/*
 * Host test for the STQC selcall decoder (toneRxDecoders.c): synthetic calls built
 * from README_STQC (sq5bpf/multimon-ng-stqc), with timing, frequency, level and noise
 * variations, plus signals that must not decode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "functions/toneRxDecoders.h"

#define FS 8000
static const double toneHz[5] = { 980.0, 1197.0, 1446.0, 1795.0, 2105.0 };

static int failures;
static uint8_t gotSymbols[8][STQC_MAX_SYMBOLS];
static uint8_t gotLen[8];
static int gotCount;

static void onGroup(const uint8_t *symbols, uint8_t len)
{
	if (gotCount < 8)
	{
		memcpy(gotSymbols[gotCount], symbols, len);
		gotLen[gotCount] = len;
	}
	gotCount++;
}

static int16_t buf[FS * 20];
static int nbuf;
static double phase;
static unsigned rng = 12345;

static double noise(void)
{
	// sum of uniforms ~ gaussian, unit variance
	double s = 0;
	for (int i = 0; i < 12; i++)
	{
		rng = rng * 1103515245u + 12345u;
		s += ((rng >> 8) & 0xFFFF) / 65536.0;
	}
	return s - 6.0;
}

static void emit(double hz, double ms, double amp, double noiseAmp)
{
	int n = (int)(ms * FS / 1000.0);
	for (int i = 0; i < n && nbuf < (int)(sizeof(buf) / sizeof(buf[0])); i++)
	{
		double v = 0;
		if (hz > 0)
		{
			phase += 2.0 * M_PI * hz / FS;
			v = amp * sin(phase);
		}
		v += noiseAmp * noise();
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		buf[nbuf++] = (int16_t)v;
	}
}

typedef struct
{
	double toneMs;
	double gapMs;
	double freqScale;
	double amp;
	double noiseAmp;
} stqcSig_t;

static void emitGroup(uint16_t value, uint8_t len, const stqcSig_t *s)
{
	char sym[16];
	stqcValueToSymbolText(value, len, sym);
	for (int i = 0; i < len; i++)
	{
		emit(toneHz[sym[i] - '0'] * s->freqScale, s->toneMs, s->amp, s->noiseAmp);
	}
}

static void run(void)
{
	gotCount = 0;
	stqcRxInit(onGroup);
	// feed in DMA-sized chunks, stereo-like stride 1 here
	for (int off = 0; off < nbuf; off += 64)
	{
		int n = ((nbuf - off) < 64) ? (nbuf - off) : 64;
		stqcRxProcess(&buf[off], (uint32_t)n, 1U);
	}
}

static void check(int cond, const char *what)
{
	if (!cond)
	{
		printf("FAIL %s\n", what);
		failures++;
	}
}

static int callDecodes(uint16_t addr, uint16_t cmd, const stqcSig_t *s, char *why, size_t whySize)
{
	nbuf = 0;
	phase = 0;
	emit(0, 300, 0, s->noiseAmp);
	emitGroup(addr, 7, s);
	emit(0, s->gapMs, 0, s->noiseAmp);
	emitGroup(cmd, 8, s);
	emit(0, 400, 0, s->noiseAmp);
	run();

	if (gotCount != 2)
	{
		snprintf(why, whySize, "%d groups", gotCount);
		return 0;
	}
	if ((gotLen[0] != 7) || (stqcSymbolsToValue(gotSymbols[0], 7) != addr))
	{
		snprintf(why, whySize, "address len %d value %u", gotLen[0], stqcSymbolsToValue(gotSymbols[0], gotLen[0]));
		return 0;
	}
	if ((gotLen[1] != 8) || (stqcSymbolsToValue(gotSymbols[1], 8) != cmd))
	{
		snprintf(why, whySize, "command len %d value %u", gotLen[1], stqcSymbolsToValue(gotSymbols[1], gotLen[1]));
		return 0;
	}
	return 1;
}

int main(void)
{
	char text[16];
	char why[64];

	// README example: 1231 in 8 symbols -> 40103034 -> 2105, 980, 1197, 980, 1795, 980, 1795, 2105 Hz
	stqcValueToSymbolText(1231, 8, text);
	check(strcmp(text, "40103034") == 0, "README encode example");
	{
		const uint8_t s[8] = { 4, 0, 1, 0, 3, 0, 3, 4 };
		check(stqcSymbolsToValue(s, 8) == 1231, "README decode example");
	}
	// round trip, and no two equal symbols in a row (what makes tone splitting possible)
	for (uint32_t v = 0; v < 65536; v += 7)
	{
		uint8_t s[8];
		stqcValueToSymbolText((uint16_t)v, 8, text);
		for (int i = 0; i < 8; i++)
		{
			s[i] = (uint8_t)(text[i] - '0');
			if ((i > 0) && (s[i] == s[i - 1]))
			{
				check(0, "repeated symbol");
			}
		}
		if (stqcSymbolsToValue(s, 8) != v)
		{
			printf("FAIL round trip %u\n", (unsigned)v);
			failures++;
			break;
		}
	}

	static const uint16_t addrs[] = { 0, 1, 1231, 5461, 16383, 4096, 2730 };
	static const uint16_t cmds[] = { 0, 65535, 1231, 21845, 43690, 4660, 9 };
	static const stqcSig_t sigs[] =
	{
		{ 100, 200, 1.000, 8000, 0 },     // nominal
		{ 100, 200, 1.000, 3000, 600 },   // ~14 dB SNR
		{ 100, 200, 1.000, 1500, 600 },   // ~8 dB SNR
		{ 100, 200, 1.020, 8000, 200 },   // tones 2 % high
		{ 100, 200, 0.980, 8000, 200 },   // tones 2 % low
		{ 70, 150, 1.000, 8000, 200 },    // fast sender
		{ 130, 300, 1.000, 8000, 200 },   // slow sender
		{ 100, 200, 1.000, 30000, 200 },  // loud
		{ 100, 200, 1.000, 700, 30 },     // quiet, clean
		{ 100, 200, 1.000, 1000, 600 },   // ~5 dB SNR
	};
	static const char *sigNames[] = { "nominal", "14dB", "8dB", "+2%", "-2%", "70ms", "130ms", "loud", "quiet", "5dB" };

	for (size_t si = 0; si < (sizeof(sigs) / sizeof(sigs[0])); si++)
	{
		int ok = 0;
		int total = 0;
		for (size_t i = 0; i < (sizeof(addrs) / sizeof(addrs[0])); i++)
		{
			total++;
			if (callDecodes(addrs[i], cmds[i], &sigs[si], why, sizeof(why)))
			{
				ok++;
			}
			else
			{
				printf("  %s: A%u C%u -> %s\n", sigNames[si], addrs[i], cmds[i], why);
			}
		}
		printf("%-8s %d/%d\n", sigNames[si], ok, total);
		check(ok == total, sigNames[si]);
	}

	// Must not decode: noise, a steady tone, DTMF digits, a voice-like tone sweep
	for (int level = 300; level <= 12000; level *= 2)
	{
		nbuf = 0;
		emit(0, 19000, 0, level);
		run();
		if (gotCount != 0)
		{
			printf("  noise %d: %d groups\n", level, gotCount);
		}
		check(gotCount == 0, "noise decodes nothing");
	}

	nbuf = 0;
	emit(1446, 2000, 0, 0);
	emit(1446, 2000, 8000, 100);
	emit(0, 500, 0, 100);
	run();
	check(gotCount == 0, "steady tone decodes nothing");

	nbuf = 0;
	for (int d = 0; d < 10; d++)
	{
		static const double rows[] = { 697, 770, 852, 941 };
		static const double cols[] = { 1209, 1336, 1477 };
		int n = (int)(0.08 * FS);
		for (int i = 0; i < n; i++)
		{
			double t = (double)i / FS;
			buf[nbuf++] = (int16_t)(5000 * sin(2 * M_PI * rows[d % 4] * t) + 5000 * sin(2 * M_PI * cols[d % 3] * t));
		}
		emit(0, 80, 0, 50);
	}
	emit(0, 500, 0, 50);
	run();
	check(gotCount == 0, "DTMF decodes nothing");

	nbuf = 0;
	{
		double f = 300, ph = 0;
		for (int i = 0; i < FS * 4; i++)
		{
			f = 600 + 500 * sin(2 * M_PI * 1.3 * i / FS) + 300 * sin(2 * M_PI * 0.37 * i / FS);
			ph += 2 * M_PI * f / FS;
			buf[nbuf++] = (int16_t)(6000 * sin(ph) + 2000 * sin(2 * ph) + 1000 * noise());
		}
	}
	emit(0, 500, 0, 50);
	run();
	check(gotCount == 0, "voice-like sweep decodes nothing");

	// A short burst (3 symbols) is not a group
	nbuf = 0;
	{
		stqcSig_t s = { 100, 200, 1.0, 8000, 100 };
		emit(0, 200, 0, 100);
		emitGroup(5, 3, &s);
		emit(0, 400, 0, 100);
	}
	run();
	check(gotCount == 0, "3 symbols are not a group");

	if (failures == 0)
	{
		printf("test_stqc: all passed\n");
	}
	return (failures ? 1 : 0);
}
