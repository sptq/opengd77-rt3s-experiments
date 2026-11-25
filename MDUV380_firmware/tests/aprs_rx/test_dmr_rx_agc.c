#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "functions/dmrRxAgc.h"

#define TEST_PCM_SAMPLE_COUNT 80U

static void assertNear(float actual, float expected, float tolerance)
{
	assert(fabsf(actual - expected) <= tolerance);
}

static void fillPcmBuffer(uint8_t *pcm, int16_t sample)
{
	uint16_t bits = (uint16_t)sample;

	for (uint32_t i = 0U; i < TEST_PCM_SAMPLE_COUNT; i++)
	{
		pcm[2U * i] = (uint8_t)(bits & 0xffU);
		pcm[(2U * i) + 1U] = (uint8_t)(bits >> 8);
	}
}

static void testPeakLimiter(void)
{
	static const uint32_t peaks[] = { 100U, 300U, 500U, 1000U, 6000U, 32768U };

	for (uint8_t setting = 1U; setting <= DMR_RX_AGC_SETTING_COUNT; setting++)
	{
		dmrRxAgcState_t state;
		float ceiling = dmrRxAgcTargetForSetting(setting) * 1.25f;

		if (ceiling > 30000.0f)
		{
			ceiling = 30000.0f;
		}

		dmrRxAgcSetStationLevel(&state, 6000U);
		for (size_t i = 0U; i < (sizeof(peaks) / sizeof(peaks[0])); i++)
		{
			uint32_t peak = peaks[i];
			int16_t positiveSample = (peak >= (uint32_t)INT16_MAX) ? INT16_MAX : (int16_t)peak;
			int16_t negativeSample = (peak >= 32768U) ? INT16_MIN : (int16_t)-((int32_t)peak);
			float gain = dmrRxAgcProcessPeak(&state, setting, peak);
			int16_t positiveOutput = dmrRxAgcScaleSample(positiveSample, gain);
			int16_t negativeOutput = dmrRxAgcScaleSample(negativeSample, gain);

			assert(gain > 0.0f);
			assert(gain <= 10.0f);
			assert(fabsf((float)positiveOutput) <= ceiling + 1.0f);
			assert(fabsf((float)negativeOutput) <= ceiling + 1.0f);
		}
	}

	// Increasing gain may be slow, but attenuation must be immediate when a
	// loud buffer follows a high-gain station.
	{
		dmrRxAgcState_t state;
		float highGain;
		float lowGain;

		dmrRxAgcSetStationLevel(&state, 300U);
		for (int i = 0; i < 40; i++)
		{
			highGain = dmrRxAgcProcessPeak(&state, 4U, 300U);
		}
		lowGain = dmrRxAgcProcessPeak(&state, 4U, 32768U);
		assert(highGain > 1.0f);
		assert(lowGain < highGain);
		assert(fabsf((float)dmrRxAgcScaleSample(INT16_MAX, lowGain)) <= 7500.0f);
	}

	// The interpolation helper implements the actual per-sample ramp used by
	// soundRefillData(), including its final sample reaching the new gain.
	{
		const float step = (2.0f - 1.0f) / 80.0f;

		assertNear(dmrRxAgcInterpolatedGain(1.0f, 2.0f, step, 0U), 1.0125f, 0.0001f);
		assertNear(dmrRxAgcInterpolatedGain(1.0f, 2.0f, step, 79U), 2.0f, 0.0001f);
		assertNear(dmrRxAgcInterpolatedGain(2.0f, 0.5f, 0.0f, 79U), 0.5f, 0.0001f);
	}
}

static void testMedianFiltering(void)
{
	dmrRxAgcState_t state;
	float stableAverage;
	float stableGain;
	float transientGain;

	dmrRxAgcSetStationLevel(&state, 6000U);
	for (int i = 0; i < 3; i++)
	{
		dmrRxAgcProcessPeak(&state, 4U, 6000U);
	}
	stableAverage = state.peakAverage;
	stableGain = state.gain;

	transientGain = dmrRxAgcProcessPeak(&state, 4U, 30000U);
	assertNear(state.peakAverage, stableAverage, 0.01f);
	assert(transientGain < stableGain);
	assert(fabsf((float)dmrRxAgcScaleSample(30000, transientGain)) <= 7500.0f);

	// Two high buffers move the median window and must attenuate the learned
	// state, unlike the isolated spike above.
	dmrRxAgcProcessPeak(&state, 4U, 30000U);
	assert(state.peakAverage > stableAverage);
	assert(state.gain < stableGain);

	for (int i = 0; i < 300; i++)
	{
		dmrRxAgcProcessPeak(&state, 4U, 6000U);
	}
	assert(state.peakAverage < 10000.0f);
	assert(state.gain > 0.5f);
}

static void testGainConvergence(void)
{
	float previousTarget = 0.0f;

	for (uint8_t setting = 1U; setting <= DMR_RX_AGC_SETTING_COUNT; setting++)
	{
		dmrRxAgcState_t state;
		float target = dmrRxAgcTargetForSetting(setting);
		float expectedGain = target / 6000.0f;

		if (previousTarget > 0.0f)
		{
			float spacing = target / previousTarget;

			assert(spacing > 1.35f);
			assert(spacing < 1.48f);
		}
		previousTarget = target;

		dmrRxAgcSetStationLevel(&state, 6000U);
		for (int i = 0; i < 200; i++)
		{
			dmrRxAgcProcessPeak(&state, setting, 6000U);
		}

		assertNear(state.gain, expectedGain, 0.02f);
		assert(state.gain >= 0.125f);
		assert(state.gain <= 10.0f);

		// A loud representative buffer remains bounded at every target.
		dmrRxAgcProcessPeak(&state, setting, 32768U);
		assert(state.gain >= 0.125f);
		assert(state.gain <= 10.0f);
	}

	// Quiet, moderate, and loud inputs all remain finite and bounded.
	{
		dmrRxAgcState_t quiet;
		dmrRxAgcState_t moderate;
		dmrRxAgcState_t loud;

		dmrRxAgcStateInit(&quiet);
		dmrRxAgcSetStationLevel(&moderate, 6000U);
		dmrRxAgcSetStationLevel(&loud, 6000U);
		for (int i = 0; i < 20; i++)
		{
			dmrRxAgcProcessPeak(&quiet, 4U, 100U);
			dmrRxAgcProcessPeak(&moderate, 4U, 3000U);
			dmrRxAgcProcessPeak(&loud, 4U, 32768U);
		}
		assert(quiet.peakAverage == 0.0f);
		assert(isfinite(moderate.gain));
		assert(isfinite(loud.gain));
		assert(moderate.gain >= 0.125f && moderate.gain <= 10.0f);
		assert(loud.gain >= 0.125f && loud.gain <= 10.0f);
	}
}

static void testConsecutiveGainContinuity(void)
{
	dmrRxAgcState_t state;
	uint8_t pcm[2U * TEST_PCM_SAMPLE_COUNT];
	uint16_t output[2U * TEST_PCM_SAMPLE_COUNT];

	// Exercise the same complete PCM operation as soundRefillData. Each next
	// buffer's first output must use the prior buffer's committed ending gain.
	fillPcmBuffer(pcm, 300);
	dmrRxAgcSetStationLevel(&state, 300U);
	for (int i = 0; i < 8; i++)
	{
		dmrRxAgcState_t expectedState;
		float from = state.applied;
		float gain;
		float gainStep;
		uint16_t expectedFirstSample;

		dmrRxAgcStateCopy(&expectedState, &state);
		gain = dmrRxAgcProcessPeak(&expectedState, 4U, 300U);
		gainStep = (gain > from) ? ((gain - from) / (float)TEST_PCM_SAMPLE_COUNT) : 0.0f;
		expectedFirstSample = (uint16_t)dmrRxAgcScaleSample(300,
				dmrRxAgcInterpolatedGain(from, gain, gainStep, 0U));

		assert(dmrRxAgcProcessPcmBuffer(&state, 4U, pcm, output, TEST_PCM_SAMPLE_COUNT) == 300U);
		assert(output[0] == expectedFirstSample);
		assertNear(state.applied, gain, 0.0001f);
	}
	assert(state.applied > 1.0f);
	assert(state.applied < 10.0f);

	// A loud buffer commits the actual peak-limited gain; recovery's first
	// sample then ramps from that limited value, not the larger smoothed gain.
	{
		dmrRxAgcState_t expectedState;
		float from = state.applied;
		float limitedGain;
		uint16_t expectedFirstSample;

		fillPcmBuffer(pcm, INT16_MIN);
		dmrRxAgcStateCopy(&expectedState, &state);
		limitedGain = dmrRxAgcProcessPeak(&expectedState, 4U, 32768U);
		assert(limitedGain < from);
		expectedFirstSample = (uint16_t)dmrRxAgcScaleSample(INT16_MIN, limitedGain);

		assert(dmrRxAgcProcessPcmBuffer(&state, 4U, pcm, output, TEST_PCM_SAMPLE_COUNT) == 32768U);
		assert(output[0] == expectedFirstSample);
		assertNear(state.applied, limitedGain, 0.0001f);

		from = state.applied;
		fillPcmBuffer(pcm, 300);
		dmrRxAgcStateCopy(&expectedState, &state);
		{
			float recoveryGain = dmrRxAgcProcessPeak(&expectedState, 4U, 300U);
			float recoveryStep = (recoveryGain > from) ? ((recoveryGain - from) / (float)TEST_PCM_SAMPLE_COUNT) : 0.0f;

			expectedFirstSample = (uint16_t)dmrRxAgcScaleSample(300,
					dmrRxAgcInterpolatedGain(from, recoveryGain, recoveryStep, 0U));
			assert(dmrRxAgcProcessPcmBuffer(&state, 4U, pcm, output, TEST_PCM_SAMPLE_COUNT) == 300U);
			assert(output[0] == expectedFirstSample);
			assertNear(state.applied, recoveryGain, 0.0001f);
		}
	}
}

static void testStationIsolation(void)
{
	dmrRxAgcOwner_t owner = { 0 };
	dmrRxAgcOwnerSnapshot_t first;
	dmrRxAgcOwnerSnapshot_t second;
	dmrRxAgcState_t learned;
	dmrRxAgcState_t fallback;
	uint8_t pcm[2U * TEST_PCM_SAMPLE_COUNT];
	uint16_t output[2U * TEST_PCM_SAMPLE_COUNT];
	int stationA = 0;
	int stationB = 0;

	dmrRxAgcOwnerPublish(&owner, &stationA, 100U, 1U);
	assert(dmrRxAgcOwnerSnapshot(&owner, &first));
	assert(first.entry == &stationA);
	assert(first.stationId == 100U);
	assert(first.stationToken == 1U);

	// A recycled entry is a different owner even when the pointer is reused.
	dmrRxAgcOwnerPublish(&owner, &stationB, 200U, 2U);
	assert(!dmrRxAgcOwnerStillCurrent(&owner, &first));
	assert(dmrRxAgcOwnerSnapshot(&owner, &second));
	assert(second.entry == &stationB);

	// Reusing the same storage for a new transmission is also distinguished by
	// the monotonically assigned token.
	dmrRxAgcOwnerPublish(&owner, &stationA, 100U, 3U);
	assert(!dmrRxAgcOwnerStillCurrent(&owner, &second));
	assert(dmrRxAgcOwnerSnapshot(&owner, &second));
	dmrRxAgcOwnerPublish(&owner, &stationA, 100U, 4U);
	assert(!dmrRxAgcOwnerStillCurrent(&owner, &second));

	owner.sequence = 1U;
	assert(!dmrRxAgcOwnerSnapshot(&owner, &second));
	owner.sequence = 0U;
	dmrRxAgcOwnerInvalidate(&owner);
	assert(!dmrRxAgcOwnerSnapshot(&owner, &second));
	assert(!dmrRxAgcOwnerStillCurrent(&owner, &second));

	// An invalid/stale owner processes into an isolated state. Running the same
	// production PCM operation must not alter the persistent learned station.
	dmrRxAgcSetStationLevel(&learned, 6000U);
	learned.applied = 0.42f;
	learned.history[0] = 6000U;
	dmrRxAgcStateCopy(&fallback, &learned);
	fillPcmBuffer(pcm, 30000);
	assert(dmrRxAgcProcessPcmBuffer(&fallback, 4U, pcm, output, TEST_PCM_SAMPLE_COUNT) == 30000U);
	assert(learned.peakAverage == 6000.0f);
	assert(learned.gain == 1.0f);
	assert(learned.applied == 0.42f);
	assert(learned.history[0] == 6000U);

	// Processing a transition buffer against the transition state leaves the
	// active station's learned state untouched.
	{
		dmrRxAgcState_t transition;
		dmrRxAgcState_t activeBefore;

		dmrRxAgcSetStationLevel(&transition, 3000U);
		dmrRxAgcStateCopy(&activeBefore, &learned);
		fillPcmBuffer(pcm, 5000);
		assert(dmrRxAgcProcessPcmBuffer(&transition, 4U, pcm, output, TEST_PCM_SAMPLE_COUNT) == 5000U);
		assert(transition.applied > 1.0f);
		assert(learned.peakAverage == activeBefore.peakAverage);
		assert(learned.gain == activeBefore.gain);
		assert(learned.applied == activeBefore.applied);
		assert(learned.history[0] == activeBefore.history[0]);
		assert(learned.history[1] == activeBefore.history[1]);
		assert(learned.history[2] == activeBefore.history[2]);
	}
}

static void testUnderrunRecovery(void)
{
	dmrRxAgcState_t state;

	dmrRxAgcSetStationLevel(&state, 6000U);
	state.gain = 0.5f;
	state.applied = 0.75f;
	state.history[0] = 1000U;
	state.history[1] = 2000U;
	state.history[2] = 3000U;
	dmrRxAgcClearHistory(&state);
	assert(state.peakAverage == 6000.0f);
	assert(state.gain == 0.5f);
	assert(state.applied == 0.75f);
	assert(state.history[0] == 0U);
	assert(state.history[1] == 0U);
	assert(state.history[2] == 0U);

	// The next normal buffer still starts from the learned level, rather than
	// receiving the unknown-station maximum gain.
	dmrRxAgcProcessPeak(&state, 4U, 6000U);
	assert(state.peakAverage == 6000.0f);
	assert(state.gain >= 0.5f);
}

static void testSettingsChangedPreservesLearnedLevels(void)
{
	dmrRxAgcState_t activeState = { 6000.0f, 2.5f, 2.0f, { 1000U, 2000U, 3000U } };
	dmrRxAgcState_t transitionState = { 3000.0f, 0.5f, 0.75f, { 400U, 500U, 600U } };

	// soundDMRRxAGCSettingsChanged applies this production helper to both
	// states while queued transition buffers remain in the ring.
	dmrRxAgcSettingsChanged(&activeState);
	dmrRxAgcSettingsChanged(&transitionState);

	assert(activeState.peakAverage == 6000.0f);
	assert(activeState.gain == 1.0f);
	assert(activeState.applied == 1.0f);
	assert(activeState.history[0] == 0U && activeState.history[1] == 0U && activeState.history[2] == 0U);
	assert(transitionState.peakAverage == 3000.0f);
	assert(transitionState.gain == 1.0f);
	assert(transitionState.applied == 1.0f);
	assert(transitionState.history[0] == 0U && transitionState.history[1] == 0U && transitionState.history[2] == 0U);
}

static void testTransitionBufferAccounting(void)
{
	assert(dmrRxAgcTransitionBuffersAfterConsume(3U, 4U) == 2U);
	assert(dmrRxAgcTransitionBuffersAfterConsume(1U, 0U) == 0U);
	assert(dmrRxAgcTransitionBuffersAfterConsume(0U, 5U) == 0U);
	// A stale count is bounded by the queue that remains after consumption.
	assert(dmrRxAgcTransitionBuffersAfterConsume(5U, 2U) == 2U);
}

static void testQuietSpeechAndNoise(void)
{
	static const uint32_t quietPeaks[] = { 0U, 199U, 200U, 300U, 400U, 500U };
	dmrRxAgcState_t state;

	dmrRxAgcStateInit(&state);
	for (size_t i = 0U; i < (sizeof(quietPeaks) / sizeof(quietPeaks[0])); i++)
	{
		for (int j = 0; j < 10; j++)
		{
			dmrRxAgcProcessPeak(&state, 4U, quietPeaks[i]);
		}
	}
	assert(state.peakAverage == 0.0f);
	assert(state.gain == 1.0f);

	// The current tuning deliberately qualifies only above 500. This test
	// records the quiet-speech limitation instead of changing the TA8BMO-derived
	// threshold without HRC6000 audio measurements.
	dmrRxAgcProcessPeak(&state, 4U, 501U);
	assert(state.peakAverage == 1500.0f);
	assert(state.gain > 1.0f);

	// Once known, intermittent quiet audio does not erase the learned station
	// level; a later speech buffer resumes normal tracking.
	dmrRxAgcSetStationLevel(&state, 6000U);
	for (int i = 0; i < 10; i++)
	{
		dmrRxAgcProcessPeak(&state, 4U, 100U);
	}
	assert(state.peakAverage == 6000.0f);
	dmrRxAgcProcessPeak(&state, 4U, 6000U);
	assert(state.peakAverage == 6000.0f);
}

static void testExactPcmBypass(void)
{
	static const uint8_t pcm[] =
	{
		0x00U, 0x00U, 0xffU, 0xffU, 0x00U, 0x80U,
		0xffU, 0x7fU, 0x34U, 0x12U, 0xcdU, 0xabU
	};
	static const uint16_t expectedBits[] =
	{
		0x0000U, 0xffffU, 0x8000U, 0x7fffU, 0x1234U, 0xabcdU
	};
	dmrRxAgcState_t state;

	for (int mode = 0; mode < 2; mode++)
	{
		state.peakAverage = 4321.0f;
		state.gain = (mode == 0) ? 2.0f : 0.5f;
		state.applied = (mode == 0) ? 1.5f : 0.25f;
		state.history[0] = 1234U;
		state.history[1] = 2345U;
		state.history[2] = 3456U;
		dmrRxAgcSetBypass(&state);

		for (size_t i = 0U; i < (sizeof(expectedBits) / sizeof(expectedBits[0])); i++)
		{
			// This is the same byte-preserving helper used by the firmware's
			// disabled-AGC and voice-prompt copy branch.
			assert((uint16_t)dmrRxAgcReadPcmSample(pcm, (uint32_t)i) == expectedBits[i]);
		}
		assert(state.peakAverage == 4321.0f);
		assert(state.gain == ((mode == 0) ? 2.0f : 0.5f));
		assert(state.applied == 1.0f);
	}
}

int main(void)
{
	testPeakLimiter();
	testMedianFiltering();
	testGainConvergence();
	testConsecutiveGainContinuity();
	testStationIsolation();
	testUnderrunRecovery();
	testSettingsChangedPreservesLearnedLevels();
	testTransitionBufferAccounting();
	testQuietSpeechAndNoise();
	testExactPcmBypass();

	puts("dmr_rx_agc: ok");
	return 0;
}
