/*
 * Fork: DTMF, CW (Morse) and STQC selcall decoders on the 8 kHz receive audio.
 * See toneRxDecoders.h.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <math.h>
#include "functions/toneRxDecoders.h"

#define SAMPLE_RATE   8000.0f
#define TWO_PI        6.28318530718f

// ---------------------------------------------------------------------------
// DTMF
// ---------------------------------------------------------------------------
#define DTMF_N                 205U
#define DTMF_TONES             8U
#define DTMF_MIN_MEAN_SQUARE   400.0f   // ~ -38 dBFS rms, below this a block is silence
#define DTMF_MIN_RATIO         0.55f    // (row + col power) / total block energy
#define DTMF_MAX_TWIST         10.0f    // +-10 dB between the row and the column tone
#define DTMF_MIN_SEPARATION    5.0f     // best tone vs the 2nd best of its group (~7 dB)
#define DTMF_GOOD_BLOCKS       2U       // ~ 51 ms of tone to accept a digit
#define DTMF_RELEASE_BLOCKS    1U       // 40 ms pauses (Q.24) leave a single clean block
#define DTMF_SEPARATOR_BLOCKS  60U      // ~ 1.5 s of silence after digits -> ' '

static const float dtmfFreqs[DTMF_TONES] = { 697.0f, 770.0f, 852.0f, 941.0f, 1209.0f, 1336.0f, 1477.0f, 1633.0f };
static const char dtmfKeys[4][4] =
{
	{ '1', '2', '3', 'A' },
	{ '4', '5', '6', 'B' },
	{ '7', '8', '9', 'C' },
	{ '*', '0', '#', 'D' }
};

static struct
{
	toneRxCharCallback_t callback;
	float    coeff[DTMF_TONES];
	float    s1[DTMF_TONES];
	float    s2[DTMF_TONES];
	float    energy;
	uint32_t n;
	char     candidate;
	uint8_t  candidateCount;
	char     held;
	float    heldEnergy;  // block energy when the held digit was last seen
	uint8_t  releaseCount;
	uint16_t silenceBlocks;
	bool     pendingSeparator;
} dtmf;

void dtmfRxInit(toneRxCharCallback_t callback)
{
	memset(&dtmf, 0, sizeof(dtmf));
	dtmf.callback = callback;
	for (uint32_t i = 0; i < DTMF_TONES; i++)
	{
		dtmf.coeff[i] = 2.0f * cosf((TWO_PI * dtmfFreqs[i]) / SAMPLE_RATE);
	}
}

static char dtmfBlockDigit(void)
{
	float p[DTMF_TONES];
	float norm;

	if ((dtmf.energy / (float)DTMF_N) < DTMF_MIN_MEAN_SQUARE)
	{
		return 0;
	}

	for (uint32_t i = 0; i < DTMF_TONES; i++)
	{
		p[i] = (dtmf.s1[i] * dtmf.s1[i]) + (dtmf.s2[i] * dtmf.s2[i]) - (dtmf.coeff[i] * dtmf.s1[i] * dtmf.s2[i]);
	}

	// A pure tone of amplitude A: power ~ (A.N/2)^2, block energy = N.A^2/2
	norm = (dtmf.energy * (float)DTMF_N) / 2.0f;

	uint32_t row = 0, col = 4;
	for (uint32_t i = 1; i < 4; i++)
	{
		if (p[i] > p[row])
		{
			row = i;
		}
		if (p[i + 4] > p[col])
		{
			col = i + 4;
		}
	}

	if (((p[row] + p[col]) / norm) < DTMF_MIN_RATIO)
	{
		return 0;
	}

	if ((p[col] > (p[row] * DTMF_MAX_TWIST)) || (p[row] > (p[col] * DTMF_MAX_TWIST)))
	{
		return 0;
	}

	for (uint32_t i = 0; i < 4; i++)
	{
		if (((i != row) && ((p[i] * DTMF_MIN_SEPARATION) > p[row])) ||
				(((i + 4) != col) && ((p[i + 4] * DTMF_MIN_SEPARATION) > p[col])))
		{
			return 0;
		}
	}

	return dtmfKeys[row][col - 4];
}

static void dtmfBlockEnd(void)
{
	char d = dtmfBlockDigit();

	// A block only partly covered by the tone (end of a digit) counts as a pause,
	// so that short pauses between repeated digits are not missed.
	if ((d != 0) && (d == dtmf.held) && (dtmf.energy < (0.4f * dtmf.heldEnergy)))
	{
		d = 0;
	}
	if ((d != 0) && (d == dtmf.held) && (dtmf.energy > dtmf.heldEnergy))
	{
		dtmf.heldEnergy = dtmf.energy;
	}

	if (d != 0)
	{
		dtmf.silenceBlocks = 0;
		dtmf.releaseCount = 0;

		if (d == dtmf.candidate)
		{
			if (dtmf.candidateCount < 255U)
			{
				dtmf.candidateCount++;
			}
		}
		else
		{
			dtmf.candidate = d;
			dtmf.candidateCount = 1;
		}

		if ((dtmf.candidateCount >= DTMF_GOOD_BLOCKS) && (dtmf.held != d))
		{
			dtmf.held = d;
			dtmf.heldEnergy = dtmf.energy;
			dtmf.pendingSeparator = true;
			if (dtmf.callback)
			{
				dtmf.callback(d);
			}
		}
	}
	else
	{
		dtmf.candidate = 0;
		dtmf.candidateCount = 0;

		if (dtmf.releaseCount < 255U)
		{
			dtmf.releaseCount++;
		}
		if (dtmf.releaseCount >= DTMF_RELEASE_BLOCKS)
		{
			dtmf.held = 0;
		}

		if (dtmf.pendingSeparator && (++dtmf.silenceBlocks >= DTMF_SEPARATOR_BLOCKS))
		{
			dtmf.pendingSeparator = false;
			if (dtmf.callback)
			{
				dtmf.callback(' ');
			}
		}
	}
}

void dtmfRxProcess(const int16_t *samples, uint32_t count, uint32_t stride)
{
	for (uint32_t s = 0; s < count; s++)
	{
		float x = (float)samples[s * stride];

		for (uint32_t i = 0; i < DTMF_TONES; i++)
		{
			float s0 = x + (dtmf.coeff[i] * dtmf.s1[i]) - dtmf.s2[i];
			dtmf.s2[i] = dtmf.s1[i];
			dtmf.s1[i] = s0;
		}
		dtmf.energy += (x * x);

		if (++dtmf.n >= DTMF_N)
		{
			dtmfBlockEnd();
			memset(dtmf.s1, 0, sizeof(dtmf.s1));
			memset(dtmf.s2, 0, sizeof(dtmf.s2));
			dtmf.energy = 0.0f;
			dtmf.n = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// CW
// ---------------------------------------------------------------------------
#define CW_N                40U      // 5 ms blocks
#define CW_BLOCK_MS         5U
#define CW_BINS             9U       // 400 .. 1200 Hz, 100 Hz apart (bins are ~200 Hz wide)
#define CW_FREQ_MIN         400U
#define CW_FREQ_STEP        100U
#define CW_MIN_LEVEL        1600.0f  // amplitude^2 (~ amplitude 40) floor for a key down
#define CW_MIN_SNR          20.0f    // peak / mean noise power ratio (13 dB) needed to decode
#define CW_HYSTERESIS       1.5f
#define CW_DEBOUNCE_BLOCKS  2U
#define CW_MIN_MARK_MS      15U
#define CW_MAX_MARK_MS      700U     // longer: a carrier / tone, not Morse
#define CW_MAX_ELEMENTS     7U

typedef struct
{
	char        c;
	const char *code;
} morse_t;

static const morse_t morseTable[] =
{
	{ 'A', ".-" }, { 'B', "-..." }, { 'C', "-.-." }, { 'D', "-.." }, { 'E', "." }, { 'F', "..-." },
	{ 'G', "--." }, { 'H', "...." }, { 'I', ".." }, { 'J', ".---" }, { 'K', "-.-" }, { 'L', ".-.." },
	{ 'M', "--" }, { 'N', "-." }, { 'O', "---" }, { 'P', ".--." }, { 'Q', "--.-" }, { 'R', ".-." },
	{ 'S', "..." }, { 'T', "-" }, { 'U', "..-" }, { 'V', "...-" }, { 'W', ".--" }, { 'X', "-..-" },
	{ 'Y', "-.--" }, { 'Z', "--.." },
	{ '0', "-----" }, { '1', ".----" }, { '2', "..---" }, { '3', "...--" }, { '4', "....-" },
	{ '5', "....." }, { '6', "-...." }, { '7', "--..." }, { '8', "---.." }, { '9', "----." },
	{ '/', "-..-." }, { '?', "..--.." }, { '=', "-...-" }, { '.', ".-.-.-" }, { ',', "--..--" },
	{ '+', ".-.-." }, { '-', "-....-" }, { '@', ".--.-." }
};

static struct
{
	toneRxCharCallback_t callback;
	float    coeff[CW_BINS];
	float    s1[CW_BINS];
	float    s2[CW_BINS];
	float    avg[CW_BINS];
	uint32_t n;
	uint32_t toneBin;
	float    hi;
	float    lo;
	bool     keyed;       // debounced state
	bool     rawKeyed;
	uint8_t  rawCount;
	uint32_t stateMs;     // time in the debounced state
	float    shortAvg;    // ms
	float    longAvg;     // ms
	uint8_t  code;        // leading 1 + elements (1 = dash)
	uint8_t  elements;
	bool     invalid;
	bool     charSinceSpace;
	bool     wordSpaceSent;
	bool     hasTiming;
	uint8_t  warmup;      // blocks before the noise floor is trusted
} cw;

void cwRxInit(toneRxCharCallback_t callback)
{
	memset(&cw, 0, sizeof(cw));
	cw.callback = callback;
	for (uint32_t i = 0; i < CW_BINS; i++)
	{
		cw.coeff[i] = 2.0f * cosf((TWO_PI * (float)(CW_FREQ_MIN + (i * CW_FREQ_STEP))) / SAMPLE_RATE);
	}
	cw.shortAvg = 60.0f; // 20 WPM
	cw.longAvg = 180.0f;
	cw.code = 1;
	cw.wordSpaceSent = true;
}

uint8_t cwRxGetWpm(void)
{
	return (cw.hasTiming ? (uint8_t)((1200.0f / cw.shortAvg) + 0.5f) : 0U);
}

uint16_t cwRxGetToneHz(void)
{
	return (cw.hasTiming ? (uint16_t)(CW_FREQ_MIN + (cw.toneBin * CW_FREQ_STEP)) : 0U);
}

bool cwRxIsKeyed(void)
{
	return cw.keyed;
}

static void cwEmit(char c)
{
	if (cw.callback)
	{
		cw.callback(c);
	}
}

static void cwCharEnd(void)
{
	if ((cw.code != 1U) && (cw.invalid == false))
	{
		char c = '*'; // unknown pattern

		for (uint32_t i = 0; i < (sizeof(morseTable) / sizeof(morseTable[0])); i++)
		{
			const char *p = morseTable[i].code;
			uint8_t code = 1;

			while (*p)
			{
				code = (uint8_t)((code << 1) | ((*p == '-') ? 1U : 0U));
				p++;
			}

			if (code == cw.code)
			{
				c = morseTable[i].c;
				break;
			}
		}
		cwEmit(c);
		cw.charSinceSpace = true;
		cw.wordSpaceSent = false;
	}

	cw.code = 1;
	cw.elements = 0;
	cw.invalid = false;
}

static void cwMarkEnd(uint32_t ms)
{
	float m = (float)ms;

	if (ms < CW_MIN_MARK_MS)
	{
		return;
	}

	if (ms > CW_MAX_MARK_MS)
	{
		cw.invalid = true;
		return;
	}

	bool dash = (m > ((cw.shortAvg + cw.longAvg) / 2.0f));

	if (dash)
	{
		cw.longAvg += (0.3f * (m - cw.longAvg));
	}
	else
	{
		cw.shortAvg += (0.3f * (m - cw.shortAvg));
	}

	// keep a sane dot/dash ratio (ideal 1:3)
	if (cw.longAvg < (2.0f * cw.shortAvg))
	{
		if (dash)
		{
			cw.shortAvg = (cw.longAvg / 2.0f);
		}
		else
		{
			cw.longAvg = (cw.shortAvg * 2.0f);
		}
	}
	if (cw.longAvg > (4.0f * cw.shortAvg))
	{
		if (dash)
		{
			cw.shortAvg = (cw.longAvg / 4.0f);
		}
		else
		{
			cw.longAvg = (cw.shortAvg * 4.0f);
		}
	}
	if (cw.shortAvg < 20.0f) // 60 WPM max
	{
		cw.shortAvg = 20.0f;
	}
	cw.hasTiming = true;

	if (cw.elements >= CW_MAX_ELEMENTS)
	{
		cw.invalid = true;
	}
	else
	{
		cw.code = (uint8_t)((cw.code << 1) | (dash ? 1U : 0U));
		cw.elements++;
	}
}

static void cwBlockEnd(void)
{
	float p[CW_BINS];
	const float norm = ((float)CW_N * (float)CW_N) / 4.0f; // -> amplitude^2

	for (uint32_t i = 0; i < CW_BINS; i++)
	{
		p[i] = ((cw.s1[i] * cw.s1[i]) + (cw.s2[i] * cw.s2[i]) - (cw.coeff[i] * cw.s1[i] * cw.s2[i])) / norm;
		cw.avg[i] += (0.01f * (p[i] - cw.avg[i])); // ~ 0.5 s
		if (cw.avg[i] > cw.avg[cw.toneBin])
		{
			cw.toneBin = i;
		}
	}

	float env = p[cw.toneBin];

	// Noise floor: mean level while the key is up. Peak: instant attack, follows
	// the level while keyed, slow release otherwise.
	if (env > cw.hi)
	{
		cw.hi = env;
	}
	else if (cw.rawKeyed)
	{
		cw.hi += (0.05f * (env - cw.hi));
	}
	else
	{
		cw.hi *= 0.997f; // ~ -2.6 dB / s
	}

	float thr = sqrtf((cw.hi * cw.lo) + 1.0f);
	if (thr < CW_MIN_LEVEL)
	{
		thr = CW_MIN_LEVEL;
	}

	bool raw;
	if (cw.warmup < 40U) // 200 ms to learn the noise floor
	{
		cw.warmup++;
		cw.lo = ((cw.warmup == 1U) ? env : (cw.lo + (0.1f * (env - cw.lo))));
		cw.hi = cw.lo;
		raw = false;
	}
	else if ((cw.hi < (cw.lo * CW_MIN_SNR)) || (cw.hi < CW_MIN_LEVEL))
	{
		raw = false;
	}
	else
	{
		raw = (cw.rawKeyed ? (env > (thr / CW_HYSTERESIS)) : (env > (thr * CW_HYSTERESIS)));
	}
	cw.rawKeyed = raw;

	if ((raw == false) && (env < thr))
	{
		cw.lo += (0.05f * (env - cw.lo)); // the floor only learns from the noise
	}

	// Debounce
	if (raw != cw.keyed)
	{
		if (++cw.rawCount >= CW_DEBOUNCE_BLOCKS)
		{
			// the debounce delay is the same on both edges, so stateMs is the real length
			if (cw.keyed)
			{
				cwMarkEnd(cw.stateMs);
			}
			cw.keyed = raw;
			cw.stateMs = (cw.rawCount * CW_BLOCK_MS);
			cw.rawCount = 0;
		}
		else
		{
			cw.stateMs += CW_BLOCK_MS;
		}
	}
	else
	{
		cw.rawCount = 0;
		cw.stateMs += CW_BLOCK_MS;
	}

	// Gaps
	if (cw.keyed == false)
	{
		float dot = cw.shortAvg;

		if ((cw.code != 1U) && ((float)cw.stateMs >= (2.0f * dot)))
		{
			cwCharEnd();
		}
		else if (cw.invalid && ((float)cw.stateMs >= (2.0f * dot)))
		{
			cwCharEnd();
		}

		if (cw.charSinceSpace && (cw.wordSpaceSent == false) && ((float)cw.stateMs >= (5.0f * dot)))
		{
			cw.wordSpaceSent = true;
			cw.charSinceSpace = false;
			cwEmit(' ');
		}
	}
}

void cwRxProcess(const int16_t *samples, uint32_t count, uint32_t stride)
{
	for (uint32_t s = 0; s < count; s++)
	{
		float x = (float)samples[s * stride];

		for (uint32_t i = 0; i < CW_BINS; i++)
		{
			float s0 = x + (cw.coeff[i] * cw.s1[i]) - cw.s2[i];
			cw.s2[i] = cw.s1[i];
			cw.s1[i] = s0;
		}

		if (++cw.n >= CW_N)
		{
			cwBlockEnd();
			memset(cw.s1, 0, sizeof(cw.s1));
			memset(cw.s2, 0, sizeof(cw.s2));
			cw.n = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// STQC (receive only)
// ---------------------------------------------------------------------------
#define STQC_N                 80U      // 10 ms blocks: bins ~100 Hz wide (the tones are >= 217 Hz apart), tolerant of off-frequency senders
#define STQC_TONES             5U
#define STQC_MIN_MEAN_SQUARE   400.0f   // as DTMF: ~ -38 dBFS rms
#define STQC_MIN_RATIO         0.35f    // best tone power / block energy
#define STQC_MIN_SEPARATION    4.0f     // best tone vs the 2nd best (6 dB)
#define STQC_ACCEPT_BLOCKS     4U       // 40 ms of a clean tone makes a symbol
#define STQC_DROPOUT_BLOCKS    3U       // a tone may miss this many blocks and go on
#define STQC_MAX_TONE_BLOCKS   40U      // 400 ms: a steady tone or carrier, not a selcall
#define STQC_GROUP_END_BLOCKS  12U      // 120 ms without a tone ends a group

static const float stqcFreqs[STQC_TONES] = { 980.0f, 1197.0f, 1446.0f, 1795.0f, 2105.0f };

static struct
{
	stqcRxGroupCallback_t callback;
	float    coeff[STQC_TONES];
	float    s1[STQC_TONES];
	float    s2[STQC_TONES];
	float    energy;
	uint16_t n;
	int8_t   tone;        // tone being tracked, -1: none
	uint8_t  run;         // blocks of 'tone'
	uint8_t  miss;        // blocks without it since the last one
	bool     emitted;     // 'tone' already added to the group
	bool     invalid;     // the group saw something that is not STQC
	uint8_t  quiet;       // blocks without any tone
	uint8_t  len;
	uint8_t  symbols[STQC_MAX_SYMBOLS];
} stqc;

void stqcRxInit(stqcRxGroupCallback_t callback)
{
	memset(&stqc, 0, sizeof(stqc));
	stqc.callback = callback;
	stqc.tone = -1;
	for (uint32_t i = 0; i < STQC_TONES; i++)
	{
		stqc.coeff[i] = 2.0f * cosf((TWO_PI * stqcFreqs[i]) / SAMPLE_RATE);
	}
}

static int stqcBlockTone(void)
{
	float p[STQC_TONES];
	uint32_t best = 0;

	if ((stqc.energy / (float)STQC_N) < STQC_MIN_MEAN_SQUARE)
	{
		return -1;
	}

	for (uint32_t i = 0; i < STQC_TONES; i++)
	{
		p[i] = (stqc.s1[i] * stqc.s1[i]) + (stqc.s2[i] * stqc.s2[i]) - (stqc.coeff[i] * stqc.s1[i] * stqc.s2[i]);
		if (p[i] > p[best])
		{
			best = i;
		}
	}

	if ((p[best] / ((stqc.energy * (float)STQC_N) / 2.0f)) < STQC_MIN_RATIO)
	{
		return -1;
	}
	for (uint32_t i = 0; i < STQC_TONES; i++)
	{
		if ((i != best) && ((p[i] * STQC_MIN_SEPARATION) > p[best]))
		{
			return -1;
		}
	}
	return (int)best;
}

static void stqcGroupEnd(void)
{
	if ((stqc.invalid == false) && (stqc.len >= STQC_MIN_SYMBOLS) && stqc.callback)
	{
		stqc.callback(stqc.symbols, stqc.len);
	}
	stqc.len = 0;
	stqc.invalid = false;
}

static void stqcBlockEnd(void)
{
	int t = stqcBlockTone();

	if (t < 0)
	{
		if (stqc.tone >= 0)
		{
			if (++stqc.miss > STQC_DROPOUT_BLOCKS)
			{
				stqc.tone = -1;
			}
		}
		if (stqc.quiet < 255U)
		{
			stqc.quiet++;
		}
		if ((stqc.quiet >= STQC_GROUP_END_BLOCKS) && ((stqc.len > 0) || stqc.invalid))
		{
			stqcGroupEnd();
		}
		return;
	}

	stqc.quiet = 0;
	if (t == stqc.tone)
	{
		stqc.miss = 0;
		if (stqc.run < 255U)
		{
			stqc.run++;
		}
		if (stqc.run > STQC_MAX_TONE_BLOCKS)
		{
			stqc.invalid = true;
		}
	}
	else
	{
		stqc.tone = (int8_t)t;
		stqc.run = 1;
		stqc.miss = 0;
		stqc.emitted = false;
	}

	if ((stqc.emitted == false) && (stqc.run >= STQC_ACCEPT_BLOCKS))
	{
		stqc.emitted = true;
		if (stqc.len < STQC_MAX_SYMBOLS)
		{
			stqc.symbols[stqc.len++] = (uint8_t)t;
		}
		else
		{
			stqc.invalid = true; // longer than any STQC group
		}
	}
}

void stqcRxProcess(const int16_t *samples, uint32_t count, uint32_t stride)
{
	for (uint32_t s = 0; s < count; s++)
	{
		float x = (float)samples[s * stride];

		for (uint32_t i = 0; i < STQC_TONES; i++)
		{
			float s0 = x + (stqc.coeff[i] * stqc.s1[i]) - stqc.s2[i];
			stqc.s2[i] = stqc.s1[i];
			stqc.s1[i] = s0;
		}
		stqc.energy += (x * x);

		if (++stqc.n >= STQC_N)
		{
			stqcBlockEnd();
			memset(stqc.s1, 0, sizeof(stqc.s1));
			memset(stqc.s2, 0, sizeof(stqc.s2));
			stqc.energy = 0.0f;
			stqc.n = 0;
		}
	}
}

uint16_t stqcSymbolsToValue(const uint8_t *symbols, uint8_t len)
{
	uint32_t value = 0;
	uint8_t prev = 0; // the sender assumes a 0 before the first symbol

	for (uint8_t i = 0; i < len; i++)
	{
		uint8_t d = ((symbols[i] >= 4U) ? prev : symbols[i]);

		value = ((value * 4U) + d);
		prev = d;
	}
	return (uint16_t)value;
}

void stqcValueToSymbolText(uint16_t value, uint8_t len, char *buf)
{
	char prevSent = '0';

	for (uint8_t i = 0; i < len; i++)
	{
		char d = (char)('0' + ((value >> (2U * (len - 1U - i))) & 3U));

		buf[i] = ((d == prevSent) ? '4' : d);
		prevSent = buf[i];
	}
	buf[len] = 0;
}
