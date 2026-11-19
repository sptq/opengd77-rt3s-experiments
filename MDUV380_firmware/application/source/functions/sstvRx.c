/*
 * Fork: SSTV receive on the 8 kHz FM RX audio. See sstvRx.h.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <math.h>
#include "functions/sstvRx.h"

#define FS              8000.0f
#define MS(x)           ((x) * (FS / 1000.0f))   // ms -> samples
#define F_CENTRE        1900.0f
#define F_SYNC_THRESH   1350.0f  // sync 1200 Hz, black 1500 Hz
#define F_BLACK         1500.0f
#define F_WHITE         2300.0f
#define FIR_TAPS        11U
#define MAX_SEGMENTS    12U
#define SYNC_TOLERANCE  0.04f    // fraction of a scan unit
#define MAX_LOST_SYNCS  10U

typedef enum
{
	SEG_SKIP = 0, // porch, separator
	SEG_SYNC,
	SEG_Y,        // luminance (Robot), Y1 (PD)
	SEG_Y2,       // second luminance line (PD)
	SEG_RY,       // R-Y
	SEG_BY,       // B-Y
	SEG_RY_OR_BY, // Robot 36: R-Y on even lines, B-Y on odd ones
	SEG_R,
	SEG_G,
	SEG_B
} segType_t;

typedef struct
{
	uint8_t segType;
	float   ms;
} segment_t;

typedef enum
{
	COLOUR_YUV = 0,
	COLOUR_RGB
} colourModel_t;

typedef struct
{
	const char     *name;
	uint8_t         vis;
	uint8_t         colour;
	uint16_t        lines;        // picture lines
	uint8_t         linesPerUnit; // lines per sync period
	float           syncMs;
	float           preMs;        // extra tone after the VIS before the first unit (Scottie: first sync)
	uint8_t         numSegments;
	segment_t       seg[MAX_SEGMENTS]; // from the start of the sync
} sstvMode_t;

static const sstvMode_t modes[] =
{
	{ "Robot 36", 8, COLOUR_YUV, 240, 1, 9.0f, 0.0f, 6,
			{ { SEG_SYNC, 9.0f }, { SEG_SKIP, 3.0f }, { SEG_Y, 88.0f }, { SEG_SKIP, 4.5f }, { SEG_SKIP, 1.5f }, { SEG_RY_OR_BY, 44.0f } } },
	{ "Robot 72", 12, COLOUR_YUV, 240, 1, 9.0f, 0.0f, 9,
			{ { SEG_SYNC, 9.0f }, { SEG_SKIP, 3.0f }, { SEG_Y, 138.0f }, { SEG_SKIP, 4.5f }, { SEG_SKIP, 1.5f }, { SEG_RY, 69.0f },
					{ SEG_SKIP, 4.5f }, { SEG_SKIP, 1.5f }, { SEG_BY, 69.0f } } },
	{ "Martin M1", 44, COLOUR_RGB, 256, 1, 4.862f, 0.0f, 8,
			{ { SEG_SYNC, 4.862f }, { SEG_SKIP, 0.572f }, { SEG_G, 146.432f }, { SEG_SKIP, 0.572f }, { SEG_B, 146.432f }, { SEG_SKIP, 0.572f },
					{ SEG_R, 146.432f }, { SEG_SKIP, 0.572f } } },
	{ "Martin M2", 40, COLOUR_RGB, 256, 1, 4.862f, 0.0f, 8,
			{ { SEG_SYNC, 4.862f }, { SEG_SKIP, 0.572f }, { SEG_G, 73.216f }, { SEG_SKIP, 0.572f }, { SEG_B, 73.216f }, { SEG_SKIP, 0.572f },
					{ SEG_R, 73.216f }, { SEG_SKIP, 0.572f } } },
	// Scottie: the sync sits between the blue and the red line (one more sync before the first line)
	{ "Scottie S1", 60, COLOUR_RGB, 256, 1, 9.0f, 9.0f, 7,
			{ { SEG_SKIP, 1.5f }, { SEG_G, 138.24f }, { SEG_SKIP, 1.5f }, { SEG_B, 138.24f }, { SEG_SYNC, 9.0f }, { SEG_SKIP, 1.5f },
					{ SEG_R, 138.24f } } },
	{ "Scottie S2", 56, COLOUR_RGB, 256, 1, 9.0f, 9.0f, 7,
			{ { SEG_SKIP, 1.5f }, { SEG_G, 88.064f }, { SEG_SKIP, 1.5f }, { SEG_B, 88.064f }, { SEG_SYNC, 9.0f }, { SEG_SKIP, 1.5f },
					{ SEG_R, 88.064f } } },
	{ "PD 90", 99, COLOUR_YUV, 256, 2, 20.0f, 0.0f, 6,
			{ { SEG_SYNC, 20.0f }, { SEG_SKIP, 2.08f }, { SEG_Y, 170.24f }, { SEG_RY, 170.24f }, { SEG_BY, 170.24f }, { SEG_Y2, 170.24f } } },
	{ "PD 120", 95, COLOUR_YUV, 496, 2, 20.0f, 0.0f, 6,
			{ { SEG_SYNC, 20.0f }, { SEG_SKIP, 2.08f }, { SEG_Y, 121.6f }, { SEG_RY, 121.6f }, { SEG_BY, 121.6f }, { SEG_Y2, 121.6f } } },
	{ "PD 180", 96, COLOUR_YUV, 496, 2, 20.0f, 0.0f, 6,
			{ { SEG_SYNC, 20.0f }, { SEG_SKIP, 2.08f }, { SEG_Y, 183.04f }, { SEG_RY, 183.04f }, { SEG_BY, 183.04f }, { SEG_Y2, 183.04f } } },
};
#define NUM_MODES (sizeof(modes) / sizeof(modes[0]))

// Low-pass, cut-off ~ 900 Hz at 8 kHz (windowed sinc, Hamming), DC gain 1
static const float fir[FIR_TAPS] =
{
	-0.00349f, 0.00939f, 0.05486f, 0.12828f, 0.19651f, 0.22890f, 0.19651f, 0.12828f, 0.05486f, 0.00939f, -0.00349f
};

typedef enum
{
	VIS_LEADER = 0,   // looking for >= 200 ms of 1900 Hz
	VIS_START,        // leader seen, waiting for the 1200 Hz start bit
	VIS_BITS
} visState_t;

static struct
{
	sstvRxPixelCallback_t pixelCallback;
	uint8_t *chan[4];         // Y / Y2 / RY / BY or R / G / B lines, SSTV_RX_WIDTH each
	sstvRxState_t state;

	// demodulator
	float    oscRe, oscIm, oscStepRe, oscStepIm;
	float    iHist[FIR_TAPS], qHist[FIR_TAPS];
	uint32_t histIdx;
	float    prevRe, prevIm;
	float    freqSmooth;
	float    level;
	uint32_t renorm;

	// VIS
	visState_t visState;
	uint32_t visRun;          // samples of the current tone run
	uint32_t visPos;          // samples since the start bit began
	float    visAcc;
	uint32_t visAccN;
	uint8_t  visBits;
	uint8_t  visBitCount;
	uint8_t  lastVis;

	// picture
	const sstvMode_t *mode;
	float    segEnd[MAX_SEGMENTS];  // samples from the unit start
	float    unitLen;
	float    syncEnd;               // samples from the unit start to the end of its sync
	float    pos;                   // samples since the unit start
	uint32_t seg;
	uint32_t unit;
	uint32_t units;
	uint32_t lostSyncs;
	bool     syncSeenThisUnit;
	uint32_t lowRun;                // samples below the sync threshold
	int32_t  pixel;                 // pixel being accumulated in the current segment
	float    pixAcc;
	uint32_t pixN;
	int32_t  lastRow;
	float    sepAcc;                // Robot 36: separator tone, tells R-Y (1500 Hz) from B-Y (2300 Hz)
	uint32_t sepN;
	bool     robotOdd;
} sstv;

static uint8_t freqToValue(float f)
{
	float v = ((f - F_BLACK) * 255.0f) / (F_WHITE - F_BLACK);

	return (uint8_t)((v < 0.0f) ? 0.0f : ((v > 255.0f) ? 255.0f : v));
}

static uint8_t clamp255(float v)
{
	return (uint8_t)((v < 0.0f) ? 0.0f : ((v > 255.0f) ? 255.0f : (v + 0.5f)));
}

void sstvRxRestart(void)
{
	sstv.state = SSTV_RX_IDLE;
	sstv.visState = VIS_LEADER;
	sstv.visRun = 0;
	sstv.mode = NULL;
	sstv.lastRow = -1;
	sstv.lastVis = 0;
}

void sstvRxInit(uint8_t *scratch, sstvRxPixelCallback_t pixelCallback)
{
	memset(&sstv, 0, sizeof(sstv));
	sstv.pixelCallback = pixelCallback;
	for (uint32_t i = 0; i < 4; i++)
	{
		sstv.chan[i] = &scratch[i * SSTV_RX_WIDTH];
		memset(sstv.chan[i], 128, SSTV_RX_WIDTH);
	}
	sstv.oscRe = 1.0f;
	sstv.oscIm = 0.0f;
	sstv.oscStepRe = cosf((-2.0f * (float)M_PI * F_CENTRE) / FS);
	sstv.oscStepIm = sinf((-2.0f * (float)M_PI * F_CENTRE) / FS);
	sstv.prevRe = 1.0f;
	sstvRxRestart();
}

void sstvRxGetStatus(sstvRxStatus_t *status)
{
	status->state = sstv.state;
	status->modeName = (sstv.mode ? sstv.mode->name : "");
	status->row = (uint16_t)((sstv.lastRow < 0) ? 0 : sstv.lastRow);
	status->freqHz = ((sstv.level > 50.0f) ? (uint16_t)sstv.freqSmooth : 0U);
	status->lastVis = sstv.lastVis;
}

// --- picture ---------------------------------------------------------------------
static void startMode(const sstvMode_t *m)
{
	float t = 0.0f;

	sstv.mode = m;
	for (uint32_t i = 0; i < m->numSegments; i++)
	{
		t += MS(m->seg[i].ms);
		sstv.segEnd[i] = t;
		if (m->seg[i].segType == SEG_SYNC)
		{
			sstv.syncEnd = t;
		}
	}
	sstv.unitLen = t;
	sstv.units = (m->lines / m->linesPerUnit);
	sstv.unit = 0;
	// The VIS stop bit (30 ms) is followed by the first sync. Start the unit
	// clock at the expected sync start, the first detected sync re-times it.
	sstv.pos = 0.0f;
	sstv.seg = 0;
	sstv.pixel = -1;
	sstv.lostSyncs = 0;
	sstv.syncSeenThisUnit = true;
	sstv.lastRow = -1;
	for (uint32_t i = 0; i < 4; i++)
	{
		memset(sstv.chan[i], ((m->colour == COLOUR_YUV) && (i >= 2)) ? 128 : 0, SSTV_RX_WIDTH);
	}
	sstv.state = SSTV_RX_RECEIVING;
}

static uint8_t *segBuffer(uint8_t type)
{
	switch (type)
	{
		case SEG_Y:
		case SEG_R:
			return sstv.chan[0];
		case SEG_Y2:
		case SEG_G:
			return sstv.chan[1];
		case SEG_RY:
		case SEG_B:
			return sstv.chan[2];
		case SEG_BY:
			return sstv.chan[3];
		case SEG_RY_OR_BY:
			return (sstv.robotOdd ? sstv.chan[3] : sstv.chan[2]);
		default:
			return NULL;
	}
}

static void emitLine(uint32_t line, const uint8_t *y)
{
	int32_t row = (int32_t)((line * SSTV_RX_ROWS) / sstv.mode->lines);

	if ((row == sstv.lastRow) || (row >= (int32_t)SSTV_RX_ROWS))
	{
		return;
	}
	sstv.lastRow = row;

	for (uint32_t x = 0; x < SSTV_RX_WIDTH; x++)
	{
		uint8_t r, g, b;

		if (sstv.mode->colour == COLOUR_RGB)
		{
			r = sstv.chan[0][x];
			g = sstv.chan[1][x];
			b = sstv.chan[2][x];
		}
		else
		{
			float yy = 1.164f * ((float)y[x] - 16.0f);
			float cr = ((float)sstv.chan[2][x] - 128.0f);
			float cb = ((float)sstv.chan[3][x] - 128.0f);

			r = clamp255(yy + (1.596f * cr));
			g = clamp255(yy - (0.813f * cr) - (0.391f * cb));
			b = clamp255(yy + (2.018f * cb));
		}

		if (sstv.pixelCallback)
		{
			sstv.pixelCallback((uint16_t)x, (uint16_t)row, r, g, b);
		}
	}
}

static void unitEnd(void)
{
	const sstvMode_t *m = sstv.mode;

	if (m->linesPerUnit == 2U)
	{
		emitLine((sstv.unit * 2U), sstv.chan[0]);
		emitLine((sstv.unit * 2U) + 1U, sstv.chan[1]);
	}
	else if (m->seg[5].segType == SEG_RY_OR_BY) // Robot 36: a row once both chroma lines are in
	{
		if (sstv.robotOdd)
		{
			emitLine(sstv.unit, sstv.chan[0]);
		}
	}
	else
	{
		emitLine(sstv.unit, sstv.chan[0]);
	}

	if (sstv.syncSeenThisUnit == false)
	{
		sstv.lostSyncs++;
	}
	else
	{
		sstv.lostSyncs = 0;
	}
	sstv.syncSeenThisUnit = false;

	sstv.unit++;
	if ((sstv.unit >= sstv.units) || (sstv.lostSyncs >= MAX_LOST_SYNCS))
	{
		sstv.state = SSTV_RX_DONE;
		sstv.visState = VIS_LEADER;
		sstv.visRun = 0;
	}
}

static void flushPixel(void)
{
	if ((sstv.pixel >= 0) && (sstv.pixN > 0))
	{
		uint8_t *buf = segBuffer(sstv.mode->seg[sstv.seg].segType);

		if (buf)
		{
			buf[sstv.pixel] = freqToValue(sstv.pixAcc / (float)sstv.pixN);
		}
	}
	sstv.pixel = -1;
	sstv.pixAcc = 0.0f;
	sstv.pixN = 0;
}

static void pictureSample(float f)
{
	const sstvMode_t *m = sstv.mode;

	// Sync tracking: a low run of about the sync length re-times the unit clock
	if (f < F_SYNC_THRESH)
	{
		sstv.lowRun++;
	}
	else
	{
		float syncLen = MS(m->syncMs);

		if ((sstv.lowRun >= (uint32_t)(syncLen * 0.6f)) && (sstv.lowRun <= (uint32_t)(syncLen * 1.6f)))
		{
			float tol = (sstv.unitLen * SYNC_TOLERANCE);

			if (fabsf(sstv.pos - (sstv.unitLen + sstv.syncEnd)) < tol)
			{
				// end of the unit not reached yet (we are slower than the sender)
				flushPixel();
				unitEnd();
				if (sstv.state != SSTV_RX_RECEIVING)
				{
					sstv.lowRun = 0;
					return;
				}
				sstv.pos = sstv.syncEnd;
				sstv.seg = 0;
				sstv.syncSeenThisUnit = true;
			}
			else if (fabsf(sstv.pos - sstv.syncEnd) < tol)
			{
				flushPixel();
				sstv.pos = sstv.syncEnd;
				sstv.seg = 0; // re-found below
				sstv.syncSeenThisUnit = true;
			}
		}
		sstv.lowRun = 0;
	}

	// Segment / pixel bookkeeping
	while ((sstv.seg < m->numSegments) && (sstv.pos >= sstv.segEnd[sstv.seg]))
	{
		flushPixel();
		sstv.seg++;

		if ((m->seg[5].segType == SEG_RY_OR_BY) && (sstv.seg == 5U))
		{
			sstv.robotOdd = ((sstv.sepN > 0) ? ((sstv.sepAcc / (float)sstv.sepN) > 1900.0f) : ((sstv.unit & 1U) != 0U));
			sstv.sepAcc = 0.0f;
			sstv.sepN = 0;
		}
	}

	if ((m->seg[5].segType == SEG_RY_OR_BY) && (sstv.seg == 3U))
	{
		sstv.sepAcc += f;
		sstv.sepN++;
	}

	if (sstv.seg >= m->numSegments)
	{
		flushPixel();
		unitEnd();
		if (sstv.state != SSTV_RX_RECEIVING)
		{
			return;
		}
		sstv.pos -= sstv.unitLen;
		sstv.seg = 0;
	}
	else if (m->seg[sstv.seg].segType != SEG_SKIP)
	{
		float segStart = ((sstv.seg == 0) ? 0.0f : sstv.segEnd[sstv.seg - 1]);
		int32_t px = (int32_t)(((sstv.pos - segStart) * (float)SSTV_RX_WIDTH) / (sstv.segEnd[sstv.seg] - segStart));

		if (px >= (int32_t)SSTV_RX_WIDTH)
		{
			px = (SSTV_RX_WIDTH - 1);
		}
		if (px != sstv.pixel)
		{
			flushPixel();
			sstv.pixel = px;
		}
		sstv.pixAcc += f;
		sstv.pixN++;
	}

	sstv.pos += 1.0f;
}

// --- VIS --------------------------------------------------------------------------
// 1900 Hz leader 300 ms, 1200 Hz break 10 ms, leader 300 ms, start bit 1200 Hz,
// 7 data bits LSB first (1100 Hz = 1, 1300 Hz = 0), even parity, stop bit 1200 Hz; 30 ms each.
static void visSample(float f)
{
	switch (sstv.visState)
	{
		case VIS_LEADER:
			if (fabsf(f - 1900.0f) < 120.0f)
			{
				if (++sstv.visRun >= (uint32_t)MS(200.0f))
				{
					sstv.visState = VIS_START;
					sstv.visRun = 0;
				}
			}
			else
			{
				sstv.visRun = 0;
			}
			break;

		case VIS_START:
			if (fabsf(f - 1200.0f) < 120.0f)
			{
				// the 10 ms break between the leaders is too short to be the start bit
				if (++sstv.visRun >= (uint32_t)MS(18.0f))
				{
					sstv.visState = VIS_BITS;
					sstv.visPos = sstv.visRun;
					sstv.visBits = 0;
					sstv.visBitCount = 0;
					sstv.visAcc = 0.0f;
					sstv.visAccN = 0;
				}
			}
			else if (fabsf(f - 1900.0f) < 120.0f)
			{
				sstv.visRun = 0; // still in a leader
			}
			else
			{
				if (++sstv.visRun > (uint32_t)MS(5.0f))
				{
					sstv.visState = VIS_LEADER; // something else
					sstv.visRun = 0;
				}
			}
			break;

		case VIS_BITS:
		{
			sstv.visPos++;
			// bit n (0 = first data bit) spans [30 + 30n, 60 + 30n] ms from the start bit
			uint32_t bitStart = (uint32_t)MS(30.0f + (30.0f * sstv.visBitCount));
			if ((sstv.visPos >= (bitStart + (uint32_t)MS(8.0f))) && (sstv.visPos < (bitStart + (uint32_t)MS(22.0f))))
			{
				sstv.visAcc += f;
				sstv.visAccN++;
			}
			else if ((sstv.visPos >= (bitStart + (uint32_t)MS(22.0f))) && (sstv.visAccN > 0))
			{
				float avg = (sstv.visAcc / (float)sstv.visAccN);
				bool one = (avg < 1200.0f);

				if ((avg < 1000.0f) || (avg > 1400.0f))
				{
					sstv.visState = VIS_LEADER; // not a VIS
					sstv.visRun = 0;
					break;
				}

				if (sstv.visBitCount < 7U)
				{
					sstv.visBits |= (uint8_t)((one ? 1U : 0U) << sstv.visBitCount);
				}
				else
				{
					uint8_t ones = 0;
					for (uint32_t i = 0; i < 7; i++)
					{
						ones += ((sstv.visBits >> i) & 1U);
					}
					ones += (one ? 1U : 0U);

					sstv.visState = VIS_LEADER;
					sstv.visRun = 0;
					if ((ones & 1U) == 0U) // even parity
					{
						sstv.lastVis = sstv.visBits; // cleared below when the mode is known
						for (uint32_t i = 0; i < NUM_MODES; i++)
						{
							if (modes[i].vis == sstv.visBits)
							{
								sstv.lastVis = 0;
								startMode(&modes[i]);
								// we are 22 ms into the parity bit: the stop bit (30 ms) follows, then the first sync
								sstv.pos = -(MS(8.0f) + MS(30.0f) + MS(modes[i].preMs));
								break;
							}
						}
					}
					break;
				}
				sstv.visBitCount++;
				sstv.visAcc = 0.0f;
				sstv.visAccN = 0;
			}
			break;
		}
	}
}

void sstvRxProcess(const int16_t *samples, uint32_t count, uint32_t stride)
{
	for (uint32_t s = 0; s < count; s++)
	{
		float x = (float)samples[s * stride];

		// mix down to 0 Hz around the 1900 Hz centre
		float re = x * sstv.oscRe;
		float im = x * sstv.oscIm;
		float t = (sstv.oscRe * sstv.oscStepRe) - (sstv.oscIm * sstv.oscStepIm);
		sstv.oscIm = (sstv.oscRe * sstv.oscStepIm) + (sstv.oscIm * sstv.oscStepRe);
		sstv.oscRe = t;
		if (++sstv.renorm >= 1000U)
		{
			float g = 1.0f / sqrtf((sstv.oscRe * sstv.oscRe) + (sstv.oscIm * sstv.oscIm));
			sstv.oscRe *= g;
			sstv.oscIm *= g;
			sstv.renorm = 0;
		}

		sstv.iHist[sstv.histIdx] = re;
		sstv.qHist[sstv.histIdx] = im;
		float fi = 0.0f, fq = 0.0f;
		uint32_t k = sstv.histIdx;
		for (uint32_t i = 0; i < FIR_TAPS; i++)
		{
			fi += (fir[i] * sstv.iHist[k]);
			fq += (fir[i] * sstv.qHist[k]);
			k = ((k == 0) ? (FIR_TAPS - 1U) : (k - 1U));
		}
		sstv.histIdx = ((sstv.histIdx + 1U) % FIR_TAPS);

		// instantaneous frequency from the phase step
		float dRe = (fi * sstv.prevRe) + (fq * sstv.prevIm);
		float dIm = (fq * sstv.prevRe) - (fi * sstv.prevIm);
		sstv.prevRe = fi;
		sstv.prevIm = fq;
		float f = F_CENTRE + ((atan2f(dIm, dRe) * FS) / (2.0f * (float)M_PI));

		float mag = fabsf(fi) + fabsf(fq);
		sstv.level += (0.001f * (mag - sstv.level));
		sstv.freqSmooth += (0.01f * (f - sstv.freqSmooth));

		if (sstv.state == SSTV_RX_RECEIVING)
		{
			pictureSample(f);
		}
		else
		{
			visSample(f);
		}
	}
}
