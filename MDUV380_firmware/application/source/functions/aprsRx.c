/*
 * Fork: APRS receive (AFSK 1200) - capture control, frame history and
 * station list. See aprsRx.h. Parsing is in aprsRxParse.c.
 *
 * Data flow: I2S DMA IRQ -> aprsRxProcessI2SBuffer() -> decoder -> frame
 * callback (IRQ context) -> small lock-free queue -> aprsRxTick() (main loop)
 * -> history + station list (+ notification in background mode).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <stdio.h>
#include "main.h"
#include "functions/aprsRx.h"
#include "functions/aprs.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/trx.h"
#include "functions/voicePrompts.h"
#include "interfaces/i2s.h"
#include "user_interface/menuSystem.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(I2S_APRS_RX)

#define QUEUE_SIZE              3U   // frames waiting for the main loop (one slot always free)
#define NO_CLOCK_RETRY_MS       1000U
#define LEVEL_WINDOW_MS         1000U
#define NOTIFICATION_MS         2500U

static APRS_RX_CCMRAM aprsRxFrame_t queue[QUEUE_SIZE];   // CCM: not zeroed, only read below queueHead
static volatile uint32_t queueHead; // written by the IRQ
static volatile uint32_t queueTail; // written by the main loop
static volatile uint16_t peakLevel; // written by the IRQ, reset by the main loop

static APRS_RX_CCMRAM aprsRxFrame_t history[APRS_RX_HISTORY_SIZE]; // CCM: not zeroed, only read below historyCount
static uint32_t historyNewest;
static uint32_t historyCount;
static uint32_t generation;

static aprsRxStation_t stations[APRS_RX_MAX_STATIONS]; // [0] = last heard
static uint32_t stationCount;

static bool monitorOn = false;
static bool backgroundOn = false;
static bool enabled = false;
static aprsRxState_t state = APRS_RX_STATE_OFF;
static uint32_t lastNoClockTime;
static uint32_t levelTime;
static uint8_t levelPercent;

// IRQ context
static void frameReceived(const uint8_t *frame, uint16_t len)
{
	uint32_t head = queueHead;
	uint32_t next = ((head + 1U) % QUEUE_SIZE);

	if (next == queueTail)
	{
		return; // main loop too slow, drop
	}

	queue[head].truncated = (len > APRS_RX_STORED_LEN);
	if (queue[head].truncated)
	{
		len = APRS_RX_STORED_LEN;
	}
	memcpy(queue[head].data, frame, len);
	queue[head].len = len;
	__DMB(); // data before index
	queueHead = next;
}

// IRQ context (I2S DMA half / full transfer), 'frames' stereo frames, left channel used.
void aprsRxProcessI2SBuffer(const int16_t *interleaved, uint32_t frames)
{
	uint16_t peak = peakLevel;

	for (uint32_t i = 0; i < frames; i++)
	{
		int16_t v = interleaved[i * 2U];
		uint16_t a = (uint16_t)((v < 0) ? -(int32_t)v : v);
		if (a > peak)
		{
			peak = a;
		}
	}
	peakLevel = peak;

	aprsRxDecoderProcess(interleaved, frames, 2U);
}

static bool captureAllowed(void)
{
	return ((trxGetMode() == RADIO_MODE_ANALOG) && (trxTransmissionEnabled == false) &&
			(aprsBeaconingIsTransmitting() == false) &&
			(soundMelodyIsPlaying() == false) && (voicePromptsIsPlaying() == false));
}

static void updateEnabled(void)
{
	bool enable = (monitorOn || backgroundOn);

	if (enable == enabled)
	{
		return;
	}

	enabled = enable;
	i2sAprsRxStop();

	if (enable)
	{
		aprsRxDecoderInit(frameReceived);
		queueHead = queueTail = 0U;
		peakLevel = 0U;
		levelPercent = 0U;
		levelTime = ticksGetMillis();
		lastNoClockTime = 0U;
		state = APRS_RX_STATE_PAUSED;
	}
	else
	{
		state = APRS_RX_STATE_OFF;
	}
}

void aprsRxSetMonitor(bool on)
{
	monitorOn = on;
	updateEnabled();
}

void aprsRxSetBackground(bool on)
{
	backgroundOn = on;
	updateEnabled();
}

bool aprsRxGetBackground(void)
{
	return backgroundOn;
}

void aprsRxToggleBackground(void)
{
	aprsRxSetBackground(!backgroundOn);
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, (backgroundOn ? "APRS RX: on" : "APRS RX: off"), false);
}

uint32_t aprsRxGetGeneration(void)
{
	return generation;
}

bool aprsRxOwnPosition(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return true;
	}
#endif
	if ((nonVolatileSettings.location.lat != 0) || (nonVolatileSettings.location.lon != 0))
	{
		*lat = settingsLocationGetLatitude();
		*lon = settingsLocationGetLongitude();
		return true;
	}
	return false;
}

// Updates the station list from a received frame. Returns the station (or NULL).
static aprsRxStation_t *stationUpdate(const aprsRxFrame_t *frame, uint32_t now, bool *hasPos)
{
	aprsRxParsed_t p;
	aprsRxPosition_t pos;
	uint32_t i;

	*hasPos = false;

	if (aprsRxParse(frame, &p) == false)
	{
		return NULL;
	}

	*hasPos = aprsRxParsePosition(&p, &pos);

	if (*hasPos && pos.killed)
	{
		*hasPos = false;
	}

	for (i = 0U; i < stationCount; i++)
	{
		if (strcmp(stations[i].name, pos.name) == 0)
		{
			break;
		}
	}

	aprsRxStation_t st;

	if (i < stationCount)
	{
		st = stations[i];
		memmove(&stations[1], &stations[0], (i * sizeof(aprsRxStation_t)));
	}
	else
	{
		memset(&st, 0, sizeof(st));
		strcpy(st.name, pos.name);
		st.courseDeg = st.speedKmh = -1;
		if (stationCount < APRS_RX_MAX_STATIONS)
		{
			stationCount++;
		}
		memmove(&stations[1], &stations[0], ((stationCount - 1U) * sizeof(aprsRxStation_t))); // drops the oldest when full
	}

	st.lastMs = now;
	if (st.count < 0xFFFFU)
	{
		st.count++;
	}

	if (*hasPos)
	{
		st.hasPos = true;
		st.lat = (float)pos.lat;
		st.lon = (float)pos.lon;
		st.symTable = pos.symTable;
		st.symCode = pos.symCode;
		st.courseDeg = pos.courseDeg;
		st.speedKmh = pos.speedKmh;
	}

	stations[0] = st;
	return &stations[0];
}

static void notifyBackground(const aprsRxStation_t *st, bool hasPos)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	int menu = menuSystemGetCurrentMenuNumber();
	double myLat, myLon;

	if ((st == NULL) || ((menu != UI_VFO_MODE) && (menu != UI_CHANNEL_MODE)))
	{
		return;
	}

	if (hasPos && aprsRxOwnPosition(&myLat, &myLon))
	{
		char dist[10];
		aprsRxFormatDistance(aprsRxDistanceKm(myLat, myLon, st->lat, st->lon), dist, sizeof(dist));
		snprintf(buf, sizeof(buf), "%s %s", st->name, dist);
	}
	else
	{
		snprintf(buf, sizeof(buf), "APRS %s", st->name);
	}

	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, buf, false);
}

void aprsRxTick(void)
{
	uint32_t now = ticksGetMillis();

	if (enabled == false)
	{
		return;
	}

	// Capture management
	if (captureAllowed() == false)
	{
		i2sAprsRxStop();
		state = APRS_RX_STATE_PAUSED;
	}
	else if (i2sAprsRxIsActive())
	{
		state = APRS_RX_STATE_LISTENING;
	}
	else if ((state != APRS_RX_STATE_NO_CLOCK) || ((now - lastNoClockTime) >= NO_CLOCK_RETRY_MS))
	{
		switch (i2sAprsRxStart())
		{
			case I2S_APRS_RX_STARTED:
			case I2S_APRS_RX_RUNNING:
				state = APRS_RX_STATE_LISTENING;
				break;
			case I2S_APRS_RX_NO_CLOCK:
				state = APRS_RX_STATE_NO_CLOCK;
				lastNoClockTime = now;
				break;
			case I2S_APRS_RX_BUSY:
				state = APRS_RX_STATE_PAUSED;
				break;
		}
	}

	// Audio level, latched every second
	if ((now - levelTime) >= LEVEL_WINDOW_MS)
	{
		uint32_t p = ((uint32_t)peakLevel * 100U) / 32767U;
		levelPercent = (uint8_t)((p > 100U) ? 100U : p);
		peakLevel = 0U;
		levelTime = now;
	}

	// Received frames -> history + stations
	while (queueTail != queueHead)
	{
		uint32_t tail = queueTail;
		aprsRxFrame_t *f;
		bool hasPos;

		historyNewest = ((historyNewest + 1U) % APRS_RX_HISTORY_SIZE);
		f = &history[historyNewest];
		memcpy(f->data, queue[tail].data, queue[tail].len);
		f->len = queue[tail].len;
		f->truncated = queue[tail].truncated;
		f->timeMs = now;
		if (historyCount < APRS_RX_HISTORY_SIZE)
		{
			historyCount++;
		}

		__DMB();
		queueTail = ((tail + 1U) % QUEUE_SIZE);
		generation++;

		aprsRxStation_t *st = stationUpdate(f, now, &hasPos);

		if (backgroundOn && (monitorOn == false))
		{
			notifyBackground(st, hasPos);
		}
	}
}

void aprsRxGetStatus(aprsRxStatus_t *status)
{
	aprsRxDecoderStats_t st;

	aprsRxDecoderGetStats(&st);
	status->state = state;
	status->levelPercent = levelPercent;
	status->framesOk = st.framesOk;
	status->crcErrors = st.crcErrors;
}

uint32_t aprsRxHistoryCount(void)
{
	return historyCount;
}

const aprsRxFrame_t *aprsRxHistoryGet(uint32_t index)
{
	if (index >= historyCount)
	{
		return NULL;
	}
	return &history[(historyNewest + APRS_RX_HISTORY_SIZE - index) % APRS_RX_HISTORY_SIZE];
}

void aprsRxHistoryClear(void)
{
	historyCount = 0U;
	stationCount = 0U;
}

uint32_t aprsRxStationCount(void)
{
	return stationCount;
}

const aprsRxStation_t *aprsRxStationGet(uint32_t index)
{
	return ((index < stationCount) ? &stations[index] : NULL);
}

#endif // I2S_APRS_RX
