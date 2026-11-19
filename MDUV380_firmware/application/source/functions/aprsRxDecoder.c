/*
 * Fork: APRS (AFSK 1200 / AX.25) receive decoder core. See aprsRxDecoder.h.
 *
 * Signal chain, per 8 kHz sample:
 *  - DC blocker,
 *  - mark (1200 Hz) and space (2200 Hz) quadrature correlators over one bit,
 *  - several "slicers", each comparing |mark| against |space| * gain, so that
 *    the tone twist added by FM pre/de-emphasis is covered (a frame is accepted
 *    from whichever slicer gets a valid FCS first),
 *  - per slicer: DPLL bit clock, NRZI decoding, HDLC deframing / bit
 *    unstuffing, FCS (CRC-16/X.25) check.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <math.h>
#include "functions/aprsRxDecoder.h"

#define CORR_LEN              7U    // ~ one bit at 8 kHz (6.67 samples)
#define NUM_SLICERS           4U
#define REFERENCE_SLICER      1U    // gain 1.0, used for the CRC error counter
#define PLL_STEP              ((uint32_t)(((uint64_t)APRS_RX_BAUD_RATE << 32) / APRS_RX_SAMPLE_RATE))
#define PLL_INERTIA           0.74f
#define ENVELOPE_ALPHA        0.5f  // 1-pole low-pass on the tone envelopes (tested on host: best noise margin)
#define DEDUP_WINDOW_SAMPLES  (APRS_RX_SAMPLE_RATE / 2U) // same frame from another slicer
#define FCS_GOOD_RESIDUE      0xF0B8U

static const float spaceGains[NUM_SLICERS] = { 0.6f, 1.0f, 1.7f, 2.8f };

typedef struct
{
	int32_t  pll;
	bool     lastLevel;      // last demodulated level (mark = true)
	bool     lastSampled;    // level at the previous bit centre (for NRZI)
	uint8_t  rawShift;       // last 8 raw (stuffed) bits, LSB first
	uint8_t  ones;           // consecutive 1s (for bit unstuffing / abort)
	uint8_t  acc;            // byte under construction
	uint8_t  bitCount;
	bool     inFrame;
	uint16_t len;
	uint8_t  buf[APRS_RX_MAX_FRAME_LEN];
} slicer_t;

static aprsRxFrameCallback_t frameCallback;
static APRS_RX_CCMRAM slicer_t slicers[NUM_SLICERS]; // zeroed by aprsRxDecoderInit()
static float history[CORR_LEN * 2U];
static uint32_t historyIdx;
static float markCos[CORR_LEN], markSin[CORR_LEN], spaceCos[CORR_LEN], spaceSin[CORR_LEN];
static float dcPrevIn, dcPrevOut;
static float markEnv, spaceEnv;
static uint32_t sampleCounter;
static uint16_t lastFcs;
static uint16_t lastLen;
static uint32_t lastDeliveredAt;
static bool hasDelivered;
static aprsRxDecoderStats_t stats;

static uint16_t fcsCompute(const uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFFU;

	for (uint16_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
		{
			crc = (crc & 1U) ? ((crc >> 1) ^ 0x8408U) : (crc >> 1);
		}
	}
	return crc;
}

static void frameEnd(slicer_t *s, uint32_t slicerIdx)
{
	// A byte aligned frame leaves exactly the 7 first bits of the closing flag in the accumulator.
	if ((s->len < APRS_RX_MIN_FRAME_LEN) || (s->bitCount != 7U))
	{
		return;
	}

	if (fcsCompute(s->buf, s->len) != FCS_GOOD_RESIDUE)
	{
		if (slicerIdx == REFERENCE_SLICER)
		{
			stats.crcErrors++;
		}
		return;
	}

	uint16_t fcs = (uint16_t)(s->buf[s->len - 2U] | (s->buf[s->len - 1U] << 8));

	if (hasDelivered && (fcs == lastFcs) && (s->len == lastLen) && ((sampleCounter - lastDeliveredAt) < DEDUP_WINDOW_SAMPLES))
	{
		return; // already delivered by another slicer
	}

	hasDelivered = true;
	lastFcs = fcs;
	lastLen = s->len;
	lastDeliveredAt = sampleCounter;
	stats.framesOk++;

	if (frameCallback)
	{
		frameCallback(s->buf, (uint16_t)(s->len - 2U));
	}
}

static void hdlcBit(slicer_t *s, uint32_t slicerIdx, bool bit)
{
	s->rawShift = (uint8_t)((s->rawShift >> 1) | (bit ? 0x80U : 0U));

	if (s->rawShift == 0x7EU) // flag
	{
		if (s->inFrame)
		{
			frameEnd(s, slicerIdx);
		}
		s->inFrame = true;
		s->len = 0U;
		s->bitCount = 0U;
		s->acc = 0U;
		s->ones = 0U;
		return;
	}

	if (bit)
	{
		if (++s->ones >= 7U) // abort / idle
		{
			s->inFrame = false;
			return;
		}
	}
	else
	{
		bool stuffed = (s->ones == 5U);
		s->ones = 0U;
		if (stuffed)
		{
			return; // stuffed zero, drop it
		}
	}

	if (s->inFrame == false)
	{
		return;
	}

	s->acc = (uint8_t)((s->acc >> 1) | (bit ? 0x80U : 0U));
	if (++s->bitCount >= 8U)
	{
		if (s->len >= APRS_RX_MAX_FRAME_LEN)
		{
			s->inFrame = false; // too long, wait for the next flag
			return;
		}
		s->buf[s->len++] = s->acc;
		s->bitCount = 0U;
	}
}

// Fork: the slicer buffers are lent to the SSTV decoder while APRS decoding is off.
// aprsRxDecoderInit() must be called before decoding APRS again.
uint8_t *aprsRxDecoderScratch(uint32_t *size)
{
	*size = sizeof(slicers);
	return (uint8_t *)slicers;
}

void aprsRxDecoderInit(aprsRxFrameCallback_t callback)
{
	frameCallback = callback;
	memset(slicers, 0, sizeof(slicers));
	memset(history, 0, sizeof(history));
	memset(&stats, 0, sizeof(stats));
	historyIdx = 0U;
	dcPrevIn = dcPrevOut = 0.0f;
	markEnv = spaceEnv = 0.0f;
	sampleCounter = 0U;
	hasDelivered = false;

	for (uint32_t k = 0; k < CORR_LEN; k++)
	{
		float m = (2.0f * (float)M_PI * 1200.0f * (float)k) / (float)APRS_RX_SAMPLE_RATE;
		float sp = (2.0f * (float)M_PI * 2200.0f * (float)k) / (float)APRS_RX_SAMPLE_RATE;
		markCos[k] = cosf(m);
		markSin[k] = sinf(m);
		spaceCos[k] = cosf(sp);
		spaceSin[k] = sinf(sp);
	}
}

void aprsRxDecoderProcess(const int16_t *samples, uint32_t count, uint32_t stride)
{
	for (uint32_t n = 0; n < count; n++)
	{
		float x = (float)samples[n * stride];

		// DC blocker
		float y = x - dcPrevIn + (0.98f * dcPrevOut);
		dcPrevIn = x;
		dcPrevOut = y;

		// Sliding window, stored twice so that a window is always contiguous
		history[historyIdx] = y;
		history[historyIdx + CORR_LEN] = y;
		historyIdx = ((historyIdx + 1U) % CORR_LEN);
		const float *w = &history[historyIdx];

		float mi = 0.0f, mq = 0.0f, si = 0.0f, sq = 0.0f;
		for (uint32_t k = 0; k < CORR_LEN; k++)
		{
			mi += w[k] * markCos[k];
			mq += w[k] * markSin[k];
			si += w[k] * spaceCos[k];
			sq += w[k] * spaceSin[k];
		}
		markEnv += ENVELOPE_ALPHA * (sqrtf((mi * mi) + (mq * mq)) - markEnv);
		spaceEnv += ENVELOPE_ALPHA * (sqrtf((si * si) + (sq * sq)) - spaceEnv);
		float mark = markEnv;
		float space = spaceEnv;

		sampleCounter++;

		for (uint32_t i = 0; i < NUM_SLICERS; i++)
		{
			slicer_t *s = &slicers[i];
			bool level = (mark > (space * spaceGains[i]));

			if (level != s->lastLevel)
			{
				// Data transition: pull the clock so that edges sit at pll == 0
				s->pll = (int32_t)((float)s->pll * PLL_INERTIA);
				s->lastLevel = level;
			}

			int32_t prev = s->pll;
			s->pll = (int32_t)((uint32_t)s->pll + PLL_STEP);

			if ((prev > 0) && (s->pll < 0)) // wrap = bit centre
			{
				bool bit = (level == s->lastSampled); // NRZI: no change = 1
				s->lastSampled = level;
				hdlcBit(s, i, bit);
			}
		}
	}
}

void aprsRxDecoderGetStats(aprsRxDecoderStats_t *out)
{
	*out = stats;
}
