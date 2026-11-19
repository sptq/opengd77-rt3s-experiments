// Host test for aprsRxDecoder: synthesises AFSK1200 AX.25 frames at 8 kHz
// with noise / tone twist / baud error and checks they decode.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "functions/aprsRxDecoder.h"

static int decoded;
static uint8_t lastFrame[400];
static uint16_t lastLen;

static void cb(const uint8_t *f, uint16_t len)
{
	decoded++;
	memcpy(lastFrame, f, len);
	lastLen = len;
}

// --- encoder ---------------------------------------------------------------
static uint8_t bits[20000];
static int nbits;
static void putBit(int b) { bits[nbits++] = (uint8_t)b; }

static void putByte(uint8_t v, int stuff, int *ones)
{
	for (int i = 0; i < 8; i++)
	{
		int b = (v >> i) & 1;
		putBit(b);
		if (stuff)
		{
			if (b) { if (++*ones == 5) { putBit(0); *ones = 0; } }
			else *ones = 0;
		}
	}
}

static uint16_t fcs(const uint8_t *d, int len)
{
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++) { crc ^= d[i]; for (int b = 0; b < 8; b++) crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1); }
	return crc ^ 0xFFFF;
}

static void addr(uint8_t *p, const char *call, int ssid, int last)
{
	int i = 0;
	for (; i < 6 && call[i]; i++) p[i] = (uint8_t)(call[i] << 1);
	for (; i < 6; i++) p[i] = ' ' << 1;
	p[6] = (uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0));
}

static int buildFrame(uint8_t *f, const char *info)
{
	int n = 0;
	addr(f + n, "APRS", 0, 0); n += 7;
	addr(f + n, "SP5ABC", 9, 0); n += 7;
	addr(f + n, "WIDE1", 1, 0); n += 7;
	addr(f + n, "WIDE2", 2, 1); n += 7;
	f[n++] = 0x03; f[n++] = 0xF0;
	int il = (int)strlen(info); memcpy(f + n, info, il); n += il;
	uint16_t c = fcs(f, n);
	f[n++] = c & 0xFF; f[n++] = c >> 8;
	return n;
}

static void encodeFrame(const uint8_t *f, int len, int preFlags, int postFlags)
{
	int ones = 0;
	for (int i = 0; i < preFlags; i++) putByte(0x7E, 0, &ones);
	ones = 0;
	for (int i = 0; i < len; i++) putByte(f[i], 1, &ones);
	for (int i = 0; i < postFlags; i++) putByte(0x7E, 0, &ones);
}

static double gauss(void)
{
	double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
	return sqrt(-2 * log(u)) * cos(2 * M_PI * v);
}

// Modulate bits (NRZI) -> 8 kHz samples. spaceAmp relative to mark (twist), snr as noise std / amp.
static int modulate(int16_t *out, int maxOut, double baud, double spaceAmp, double noise, double startPhase, int lead)
{
	int n = 0;
	double phase = startPhase, t = 0;
	int tone = 1; // mark
	for (int i = 0; i < lead; i++) out[n++] = (int16_t)(noise * 8000 * gauss());
	for (int b = 0; b < nbits; b++)
	{
		if (bits[b] == 0) tone = !tone; // NRZI: 0 = change
		t += 8000.0 / baud;
		while (t >= 1.0 && n < maxOut)
		{
			double f = tone ? 1200.0 : 2200.0;
			double a = tone ? 1.0 : spaceAmp;
			phase += 2 * M_PI * f / 8000.0;
			double s = 8000 * (a * sin(phase) + noise * gauss());
			if (s > 32767) s = 32767; if (s < -32768) s = -32768;
			out[n++] = (int16_t)s;
			t -= 1.0;
		}
	}
	for (int i = 0; i < lead && n < maxOut; i++) out[n++] = (int16_t)(noise * 8000 * gauss());
	return n;
}

static int16_t audio[400000];

static int run(const char *name, double baud, double spaceAmp, double noise, int frames)
{
	uint8_t f[400];
	int len = buildFrame(f, "!5213.45N/02100.12E>Test APRS RX decoder 1234567890");
	decoded = 0;
	aprsRxDecoderInit(cb);
	int16_t inter[2 * 160];
	for (int k = 0; k < frames; k++)
	{
		nbits = 0;
		encodeFrame(f, len, 30, 3);
		int n = modulate(audio, 400000, baud, spaceAmp, noise, k * 0.7, 800 + (k * 37) % 400);
		// feed as interleaved stereo blocks of 160 like the I2S DMA
		for (int i = 0; i < n; i += 160)
		{
			int c = (n - i) < 160 ? (n - i) : 160;
			for (int j = 0; j < c; j++) { inter[2 * j] = audio[i + j]; inter[2 * j + 1] = 0; }
			aprsRxDecoderProcess(inter, c, 2);
		}
	}
	aprsRxDecoderStats_t st;
	aprsRxDecoderGetStats(&st);
	int ok = (lastLen == len - 2) && (memcmp(lastFrame, f, len - 2) == 0);
	printf("%-34s %3d/%d decoded  crcErr=%u  content=%s\n", name, decoded, frames, st.crcErrors, ok ? "OK" : "BAD");
	return decoded;
}

int main(void)
{
	srand(1);
	int fails = 0;
	fails += run("clean", 1200, 1.0, 0.0, 20) != 20;
	fails += run("baud +1%", 1212, 1.0, 0.0, 20) != 20;
	fails += run("baud -1%", 1188, 1.0, 0.0, 20) != 20;
	fails += run("de-emph twist (space -6dB)", 1200, 0.5, 0.0, 20) != 20;
	fails += run("pre-emph twist (space +6dB)", 1200, 2.0, 0.0, 20) != 20;
	fails += run("noise 0.2", 1200, 1.0, 0.2, 20) < 19;
	run("noise 0.35", 1200, 1.0, 0.35, 20);
	run("noise 0.5", 1200, 1.0, 0.5, 20);
	run("noise 0.3 + twist -6dB + baud +0.5%", 1206, 0.5, 0.3, 20);
	printf(fails ? "FAILED (%d)\n" : "ALL REQUIRED PASSED\n", fails);
	return fails;
}
