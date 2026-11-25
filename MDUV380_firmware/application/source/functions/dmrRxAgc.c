/*
 * Copyright (C) 2025 Roger Clark, VK3KYY / G4KYF
 *
 * See dmrRxAgc.h for the redistribution terms.
 */

// The AGC target curve, thresholds, median window, release/rise response, and
// limiter tuning are adapted from TA8BMO's OpenGD77 implementation. Station
// lifecycle, exact bypass behavior, synchronization, and host-test extraction
// are maintained separately in this fork.

#include "functions/dmrRxAgc.h"

#include <stddef.h>

#define DMR_RX_AGC_GATE      200U
#define DMR_RX_AGC_TALK      500U
#define DMR_RX_AGC_FLOOR     1500.0f
#define DMR_RX_AGC_RELEASE   0.02f
#define DMR_RX_AGC_RISE      0.1f
#define DMR_RX_AGC_GAIN_MAX  10.0f
#define DMR_RX_AGC_GAIN_MIN  0.125f
#define DMR_RX_AGC_LIMIT     30000.0f

static const float dmrRxAgcTarget[DMR_RX_AGC_SETTING_COUNT] =
{
	2129.0f, 3007.0f, 4248.0f, 6000.0f,
	8475.0f, 11972.0f, 16910.0f, 23886.0f
};

static bool dmrRxAgcOwnerRead(const volatile dmrRxAgcOwner_t *owner, dmrRxAgcOwnerSnapshot_t *snapshot)
{
	uint32_t sequenceBefore = owner->sequence;
	uint32_t sequenceAfter;
	bool valid;

	if ((sequenceBefore & 1U) != 0U)
	{
		return false;
	}

	snapshot->entry = owner->entry;
	snapshot->stationId = owner->stationId;
	snapshot->stationToken = owner->stationToken;
	valid = owner->valid;
	sequenceAfter = owner->sequence;

	if ((sequenceBefore != sequenceAfter) || ((sequenceAfter & 1U) != 0U))
	{
		return false;
	}

	return valid;
}

static uint16_t clampPeak(uint32_t peak)
{
	return (uint16_t)((peak > UINT16_MAX) ? UINT16_MAX : peak);
}

static float medianPeak(const volatile dmrRxAgcState_t *state)
{
	uint16_t a = state->history[0];
	uint16_t b = state->history[1];
	uint16_t c = state->history[2];

	if (a == 0U)
	{
		return (float)((b > c) ? b : c);
	}

	return (float)((a > b) ? ((b > c) ? b : ((a > c) ? c : a)) :
			((a > c) ? a : ((b > c) ? c : b)));
}

void dmrRxAgcStateInit(volatile dmrRxAgcState_t *state)
{
	state->peakAverage = DMR_RX_AGC_DEFAULT_PEAK_SAMPLES;
	state->gain = 1.0f;
	state->applied = 1.0f;
	state->history[0] = 0U;
	state->history[1] = 0U;
	state->history[2] = 0U;
}

void dmrRxAgcStateCopy(volatile dmrRxAgcState_t *destination, const volatile dmrRxAgcState_t *source)
{
	destination->peakAverage = source->peakAverage;
	destination->gain = source->gain;
	destination->applied = source->applied;
	destination->history[0] = source->history[0];
	destination->history[1] = source->history[1];
	destination->history[2] = source->history[2];
}

void dmrRxAgcClearHistory(volatile dmrRxAgcState_t *state)
{
	state->history[0] = 0U;
	state->history[1] = 0U;
	state->history[2] = 0U;
}

void dmrRxAgcSetStationLevel(volatile dmrRxAgcState_t *state, uint16_t rememberedPeak)
{
	state->peakAverage = (float)rememberedPeak;
	state->gain = 1.0f;
	state->applied = 1.0f;
	dmrRxAgcClearHistory(state);
}

void dmrRxAgcSettingsChanged(volatile dmrRxAgcState_t *state)
{
	// Keep the station's learned level, but do not carry a sample history or
	// an old output gain across a menu/startup setting transition.
	state->gain = 1.0f;
	state->applied = 1.0f;
	dmrRxAgcClearHistory(state);
}

void dmrRxAgcSetBypass(volatile dmrRxAgcState_t *state)
{
	// Learned station state and smoothed gain remain untouched. The next DMR
	// buffer starts its interpolation from unity after a prompt or AGC bypass.
	state->applied = 1.0f;
}

void dmrRxAgcSetAppliedGain(volatile dmrRxAgcState_t *state, float gain)
{
	// The gain returned for a buffer may be peak-limited. Store that actual
	// ending gain so the next buffer's interpolation starts continuously.
	state->applied = gain;
}

float dmrRxAgcTargetForSetting(uint8_t setting)
{
	if (setting < 1U)
	{
		setting = 1U;
	}
	else if (setting > DMR_RX_AGC_SETTING_COUNT)
	{
		setting = DMR_RX_AGC_SETTING_COUNT;
	}

	return dmrRxAgcTarget[setting - 1U];
}

float dmrRxAgcProcessPeak(volatile dmrRxAgcState_t *state, uint8_t setting, uint32_t peak)
{
	float target;
	float gain;

	if (setting == 0U)
	{
		return 1.0f;
	}

	if (setting > DMR_RX_AGC_SETTING_COUNT)
	{
		setting = DMR_RX_AGC_SETTING_COUNT;
	}

	target = dmrRxAgcTargetForSetting(setting);

	if (peak > DMR_RX_AGC_GATE)
	{
		// Do not let a noise floor create the first remembered level. Once a
		// station has a level, normal gate/release tracking can resume.
		if ((state->peakAverage > 0.0f) || (peak > DMR_RX_AGC_TALK))
		{
			float level;

			state->history[0] = state->history[1];
			state->history[1] = state->history[2];
			state->history[2] = clampPeak(peak);
			level = medianPeak(state);

			if (state->peakAverage <= 0.0f)
			{
				state->peakAverage = (level > DMR_RX_AGC_FLOOR) ? level : DMR_RX_AGC_FLOOR;
			}
			else if (level > state->peakAverage)
			{
				state->peakAverage = level;
			}
			else
			{
				state->peakAverage += ((level - state->peakAverage) * DMR_RX_AGC_RELEASE);
			}
		}
	}

	if (state->peakAverage > 0.0f)
	{
		float wanted = (target / state->peakAverage);

		if (wanted > DMR_RX_AGC_GAIN_MAX)
		{
			wanted = DMR_RX_AGC_GAIN_MAX;
		}
		else if (wanted < DMR_RX_AGC_GAIN_MIN)
		{
			wanted = DMR_RX_AGC_GAIN_MIN;
		}

		state->gain = ((wanted < state->gain) ? wanted :
				(state->gain + ((wanted - state->gain) * DMR_RX_AGC_RISE)));
	}

	// Hold this buffer below a known ceiling before applying the gain, so a
	// loud first syllable cannot wrap or clip while the slow rise catches up.
	gain = state->gain;
	if (peak > 0U)
	{
		float ceiling = target * 1.25f;
		float cap;

		if (ceiling > DMR_RX_AGC_LIMIT)
		{
			ceiling = DMR_RX_AGC_LIMIT;
		}
		cap = ceiling / (float)peak;
		if (gain > cap)
		{
			gain = cap;
		}
	}

	return gain;
}

void dmrRxAgcOwnerInvalidate(volatile dmrRxAgcOwner_t *owner)
{
	owner->sequence++;
	owner->valid = false;
	owner->entry = NULL;
	owner->stationId = 0U;
	owner->stationToken = 0U;
	owner->sequence++;
}

void dmrRxAgcOwnerPublish(volatile dmrRxAgcOwner_t *owner, const void *entry, uint32_t stationId, uint32_t stationToken)
{
	owner->sequence++;
	owner->entry = entry;
	owner->stationId = stationId;
	owner->stationToken = stationToken;
	owner->valid = (entry != NULL) && (stationId != 0U) && (stationToken != 0U);
	owner->sequence++;
}

bool dmrRxAgcOwnerSnapshot(const volatile dmrRxAgcOwner_t *owner, dmrRxAgcOwnerSnapshot_t *snapshot)
{
	return dmrRxAgcOwnerRead(owner, snapshot);
}

bool dmrRxAgcOwnerStillCurrent(const volatile dmrRxAgcOwner_t *owner, const dmrRxAgcOwnerSnapshot_t *snapshot)
{
	dmrRxAgcOwnerSnapshot_t current;

	if (!dmrRxAgcOwnerRead(owner, &current))
	{
		return false;
	}

	return (current.entry == snapshot->entry) &&
			(current.stationId == snapshot->stationId) &&
			(current.stationToken == snapshot->stationToken);
}
