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
#include <math.h>
#include <stdio.h>
#include "main.h"
#include "functions/aprsRx.h"
#include "functions/toneRxDecoders.h"
#include "functions/sstvRx.h"
#include "functions/eventLog.h"
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
#if defined(PLATFORM_MDUV380)
#include "hardware/AT1846S.h"
#include "interfaces/adc.h"
#endif

#if defined(I2S_APRS_RX)

#define QUEUE_SIZE              3U   // frames waiting for the main loop (one slot always free)
#define NO_CLOCK_RETRY_MS       1000U
#define LEVEL_WINDOW_MS         1000U
#define NOTIFICATION_MS         2500U
#define MSG_NOTIFICATION_MS     5000U
#define ACK_DELAY_MS            2000U  // let the digipeaters finish first
#define ACK_QUEUE_SIZE          2U
#define MSG_RETRY_MS            30000U
#define MSG_MAX_TRIES           3U
#define TX_BUSY_RETRY_MS        3000U
#define ACK_RATE_WINDOW_MS      (10U * 60U * 1000U) // at most ACK_RATE_MAX acks in this window,
#define ACK_RATE_MAX            6U                  // so other stations cannot keep the radio transmitting

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

static APRS_RX_CCMRAM aprsRxMsgEntry_t messages[APRS_RX_MAX_MESSAGES]; // CCM: only read below messageCount
static uint32_t messageCount;

typedef struct
{
	char     to[APRS_RX_CALL_LEN];
	char     msgId[APRS_RX_MSG_ID_LEN];
	uint32_t dueMs;
	uint8_t  tries;
} ackPending_t;
static ackPending_t ackQueue[ACK_QUEUE_SIZE];
static uint32_t ackCount;
static uint32_t outNextTryMs;   // next (re)try of the pending outgoing message
static uint32_t outMsgNumber;   // last message number used
static bool txErrorShown;

// DTMF / CW text rings, written by the IRQ
static char dtmfRing[APRS_RX_TONE_TEXT_LEN];
static volatile uint32_t dtmfHead;
static char cwRing[APRS_RX_TONE_TEXT_LEN];
static volatile uint32_t cwHead;
// STQC: groups from the IRQ (one address + one command in flight), calls kept for display
#define STQC_GROUP_QUEUE   2U
#define STQC_PAIR_MS       1500U       // a command this soon after an address belongs to it
typedef struct
{
	uint8_t len;
	uint8_t symbols[STQC_MAX_SYMBOLS];
} stqcGroup_t;
static stqcGroup_t stqcQueue[STQC_GROUP_QUEUE];
static volatile uint8_t stqcQueueHead;
static volatile uint8_t stqcQueueTail;
static aprsRxStqcCall_t stqcCalls[APRS_RX_STQC_CALLS];
static uint8_t stqcCallCount;
static uint8_t stqcSerial;
static bool stqcAddrPending;       // stqcCalls[0] holds an address still waiting for its command
static uint32_t dtmfSeen;          // main loop: chars already looked at (background notification)
static char dtmfSequence[17];
static uint32_t dtmfSequenceLen;

static volatile bool sstvOn = false; // SSTV replaces the APRS / DTMF / CW decoders
static bool monitorOn = false;
static bool backgroundOn = false;
static bool enabled = false;
static aprsRxState_t state = APRS_RX_STATE_OFF;
static uint32_t lastNoClockTime;
static uint32_t levelTime;
static uint8_t levelPercent;

static void setFMRxAudioCaptureActive(bool active)
{
#if defined(PLATFORM_MDUV380)
	radioSetFMRxAudioCaptureActive(active);
#else
	UNUSED_PARAMETER(active);
#endif
}

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

	if (sstvOn)
	{
		sstvRxProcess(interleaved, frames, 2U);
		return;
	}

	aprsRxDecoderProcess(interleaved, frames, 2U);
	dtmfRxProcess(interleaved, frames, 2U);
	cwRxProcess(interleaved, frames, 2U);
	stqcRxProcess(interleaved, frames, 2U);
}

void aprsRxSetSstv(bool on, sstvRxPixelCallback_t pixelCallback)
{
	if (on == sstvOn)
	{
		return;
	}

	i2sAprsRxStop(); // no IRQ while the shared memory changes hands; aprsRxTick() restarts the capture
	setFMRxAudioCaptureActive(false);
	state = APRS_RX_STATE_PAUSED;

	if (on)
	{
		uint32_t size;
		uint8_t *scratch = aprsRxDecoderScratch(&size);

		if (size < SSTV_RX_SCRATCH_SIZE)
		{
			return;
		}
		sstvRxInit(scratch, pixelCallback);
		sstvOn = true;
	}
	else
	{
		sstvOn = false;
		aprsRxDecoderInit(frameReceived);
	}
}

bool aprsRxGetSstv(void)
{
	return sstvOn;
}

// IRQ context
static void dtmfCharReceived(char c)
{
	dtmfRing[dtmfHead % APRS_RX_TONE_TEXT_LEN] = c;
	__DMB();
	dtmfHead++;
}

// IRQ context
static void cwCharReceived(char c)
{
	cwRing[cwHead % APRS_RX_TONE_TEXT_LEN] = c;
	__DMB();
	cwHead++;
}

// IRQ context
static void stqcGroupReceived(const uint8_t *symbols, uint8_t len)
{
	uint8_t next = (uint8_t)((stqcQueueHead + 1U) % STQC_GROUP_QUEUE);

	if (next == stqcQueueTail)
	{
		return; // main loop too slow, drop
	}
	memcpy(stqcQueue[stqcQueueHead].symbols, symbols, len);
	stqcQueue[stqcQueueHead].len = len;
	__DMB(); // data before index
	stqcQueueHead = next;
}

uint32_t aprsRxStqcCount(void)
{
	return stqcCallCount;
}

const aprsRxStqcCall_t *aprsRxStqcGet(uint32_t index)
{
	return ((index < stqcCallCount) ? &stqcCalls[index] : NULL);
}

uint32_t aprsRxStqcSerial(void)
{
	return stqcSerial;
}

void aprsRxStqcClear(void)
{
	stqcCallCount = 0U;
	stqcAddrPending = false;
	stqcSerial++;
}

void aprsRxStqcFormat(const aprsRxStqcCall_t *call, char *buf, uint32_t size)
{
	if (call->addrLen && call->cmdLen)
	{
		snprintf(buf, size, "A%u C%u", (unsigned)call->addr, (unsigned)call->cmd);
	}
	else if (call->addrLen)
	{
		snprintf(buf, size, "A%u", (unsigned)call->addr);
	}
	else
	{
		snprintf(buf, size, "C%u", (unsigned)call->cmd);
	}
}

// A finished call: event log and, in the background on the main screens, a notification
static void stqcCallDone(const aprsRxStqcCall_t *call)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char text[16];
	int menu = menuSystemGetCurrentMenuNumber();

	aprsRxStqcFormat(call, text, sizeof(text));
#if defined(HAS_EVENT_LOG)
	eventLogAdd(EVENT_LOG_DTMF, "STQC %s", text);
#endif
	if (backgroundOn && (monitorOn == false) && ((menu == UI_VFO_MODE) || (menu == UI_CHANNEL_MODE)))
	{
		snprintf(buf, sizeof(buf), "STQC %s", text);
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, buf, false);
	}
}

static void stqcNewCall(uint32_t now)
{
	if (stqcCallCount < APRS_RX_STQC_CALLS)
	{
		stqcCallCount++;
	}
	memmove(&stqcCalls[1], &stqcCalls[0], ((stqcCallCount - 1U) * sizeof(stqcCalls[0])));
	memset(&stqcCalls[0], 0, sizeof(stqcCalls[0]));
	stqcCalls[0].ms = now;
}

// Main loop: pair the address (7 symbols) with the command (8 symbols) that follows it
static void stqcTick(uint32_t now)
{
	while (stqcQueueTail != stqcQueueHead)
	{
		const stqcGroup_t *g = &stqcQueue[stqcQueueTail];
		uint16_t value = stqcSymbolsToValue(g->symbols, g->len);
		uint8_t len = g->len;

		__DMB();
		stqcQueueTail = (uint8_t)((stqcQueueTail + 1U) % STQC_GROUP_QUEUE);

		if (stqcAddrPending && (len != 7U))
		{
			// the command of the pending address (usually 8 symbols, any other length is kept as the command too)
			stqcAddrPending = false;
			stqcCalls[0].cmd = value;
			stqcCalls[0].cmdLen = len;
			stqcCallDone(&stqcCalls[0]);
		}
		else
		{
			if (stqcAddrPending)
			{
				stqcAddrPending = false;
				stqcCallDone(&stqcCalls[0]); // two addresses in a row: the first had no command
			}
			stqcNewCall(now);
			if (len == 7U)
			{
				stqcCalls[0].addr = value;
				stqcCalls[0].addrLen = len;
				stqcAddrPending = true;
			}
			else
			{
				stqcCalls[0].cmd = value;
				stqcCalls[0].cmdLen = len;
				stqcCallDone(&stqcCalls[0]);
			}
		}
		stqcSerial++;
	}

	if (stqcAddrPending && ((now - stqcCalls[0].ms) > STQC_PAIR_MS))
	{
		stqcAddrPending = false;
		stqcCallDone(&stqcCalls[0]); // address only
	}
}

uint32_t aprsRxToneText(bool cw, char *out, uint32_t size, uint32_t *serial)
{
	const char *ring = (cw ? cwRing : dtmfRing);
	uint32_t head = (cw ? cwHead : dtmfHead);
	uint32_t n = ((head < APRS_RX_TONE_TEXT_LEN) ? head : APRS_RX_TONE_TEXT_LEN);

	if (n > (size - 1U))
	{
		n = (size - 1U);
	}
	for (uint32_t i = 0U; i < n; i++)
	{
		out[i] = ring[(head - n + i) % APRS_RX_TONE_TEXT_LEN];
	}
	out[n] = 0;

	if (serial)
	{
		*serial = head;
	}
	return n;
}

void aprsRxToneTextClear(void)
{
	dtmfHead = cwHead = 0U;
	dtmfSeen = 0U;
	dtmfSequenceLen = 0U;
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
	setFMRxAudioCaptureActive(false);
	sstvOn = false;

	if (enable)
	{
		aprsRxDecoderInit(frameReceived);
		dtmfRxInit(dtmfCharReceived);
		cwRxInit(cwCharReceived);
		stqcRxInit(stqcGroupReceived);
		stqcQueueHead = stqcQueueTail = 0U;
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
	settingsSetOptionBit(BIT_APRS_RX_BACKGROUND, backgroundOn);
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
	if (settingsLocationIsValid())
	{
		*lat = settingsLocationGetLatitude();
		*lon = settingsLocationGetLongitude();
		return ((fabs(*lat) <= 90.0) && (fabs(*lon) <= 180.0));
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
	bool isNew = false;

	if (i < stationCount)
	{
		st = stations[i];
		memmove(&stations[1], &stations[0], (i * sizeof(aprsRxStation_t)));
	}
	else
	{
		isNew = true;
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
	st.isObject = (strcmp(pos.name, p.src) != 0);
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

#if defined(HAS_EVENT_LOG)
	if (isNew)
	{
		double myLat, myLon;

		if (st.hasPos && aprsRxOwnPosition(&myLat, &myLon))
		{
			char dist[10];
			aprsRxFormatDistance(aprsRxDistanceKm(myLat, myLon, st.lat, st.lon), dist, sizeof(dist));
			eventLogAdd(EVENT_LOG_APRS, "%s %s", st.name, dist);
		}
		else
		{
			eventLogAdd(EVENT_LOG_APRS, "%s", st.name);
		}
	}
#endif
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

// --- Messages --------------------------------------------------------------------
bool aprsRxGetOwnCall(char *buf, uint32_t size)
{
	return aprsGetOwnCallsign(buf, size);
}

static aprsRxMsgEntry_t *messageAdd(void)
{
	if (messageCount < APRS_RX_MAX_MESSAGES)
	{
		messageCount++;
	}
	memmove(&messages[1], &messages[0], ((messageCount - 1U) * sizeof(aprsRxMsgEntry_t)));
	memset(&messages[0], 0, sizeof(aprsRxMsgEntry_t));
	return &messages[0];
}

uint32_t aprsRxMessageCount(void)
{
	return messageCount;
}

const aprsRxMsgEntry_t *aprsRxMessageGet(uint32_t index)
{
	return ((index < messageCount) ? &messages[index] : NULL);
}

uint32_t aprsRxMessageUnreadCount(void)
{
	uint32_t n = 0U;

	for (uint32_t i = 0U; i < messageCount; i++)
	{
		if (messages[i].unread)
		{
			n++;
		}
	}
	return n;
}

void aprsRxMessagesMarkRead(void)
{
	for (uint32_t i = 0U; i < messageCount; i++)
	{
		messages[i].unread = false;
	}
}

static aprsRxMsgEntry_t *messagePendingOut(void)
{
	for (uint32_t i = 0U; i < messageCount; i++)
	{
		if (messages[i].kind == APRS_MSG_OUT_PENDING)
		{
			return &messages[i];
		}
	}
	return NULL;
}

void aprsRxSendMessage(const char *to, const char *text)
{
	aprsRxMsgEntry_t *pending = messagePendingOut();

	if (pending)
	{
		pending->kind = APRS_MSG_OUT_FAILED; // only one in flight
	}

	aprsRxMsgEntry_t *m = messageAdd();

	snprintf(m->peer, sizeof(m->peer), "%s", to);
	snprintf(m->text, sizeof(m->text), "%s", text);
	if (outMsgNumber == 0U)
	{
		outMsgNumber = (ticksGetMillis() % 89U); // do not restart at 1 on every boot
	}
	outMsgNumber = ((outMsgNumber % 99U) + 1U);
	snprintf(m->msgId, sizeof(m->msgId), "%u", (unsigned)outMsgNumber);
	m->timeMs = ticksGetMillis();
	m->kind = APRS_MSG_OUT_PENDING;
	m->tries = 0U;
#if defined(HAS_EVENT_LOG)
	eventLogAdd(EVENT_LOG_MSG_OUT, "%s:%s", to, text);
#endif
	outNextTryMs = m->timeMs;
	txErrorShown = false;
}

static bool msgIdIsValid(const char *id)
{
	uint32_t n = 0U;

	for (; id[n]; n++)
	{
		char c = id[n];
		if (((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')))
		{
			continue;
		}
		return false;
	}
	return ((n >= 1U) && (n <= 5U));
}

static bool ackRateAllows(uint32_t now)
{
	static uint32_t windowStart;
	static uint32_t acksInWindow;

	if ((acksInWindow == 0U) || ((now - windowStart) >= ACK_RATE_WINDOW_MS))
	{
		windowStart = now;
		acksInWindow = 0U;
	}
	if (acksInWindow >= ACK_RATE_MAX)
	{
		return false;
	}
	acksInWindow++;
	return true;
}

static void queueAck(const char *to, const char *msgId, uint32_t now)
{
	if (msgIdIsValid(msgId) == false)
	{
		return;
	}

	for (uint32_t i = 0U; i < ackCount; i++)
	{
		if ((strcmp(ackQueue[i].to, to) == 0) && (strcmp(ackQueue[i].msgId, msgId) == 0))
		{
			return; // already queued
		}
	}

	if (ackCount >= ACK_QUEUE_SIZE)
	{
		memmove(&ackQueue[0], &ackQueue[1], ((ACK_QUEUE_SIZE - 1U) * sizeof(ackPending_t)));
		ackCount--;
	}
	snprintf(ackQueue[ackCount].to, sizeof(ackQueue[ackCount].to), "%s", to);
	snprintf(ackQueue[ackCount].msgId, sizeof(ackQueue[ackCount].msgId), "%s", msgId);
	ackQueue[ackCount].dueMs = (now + ACK_DELAY_MS);
	ackQueue[ackCount].tries = 0U;
	ackCount++;
}

static aprsInfoTxResult_t sendInfo(const char *info)
{
	i2sAprsRxStop(); // restarted by aprsRxTick() once the transmission is over
	state = APRS_RX_STATE_PAUSED;
	return aprsSendInfoFrame(info);
}

static void showTxError(aprsInfoTxResult_t res)
{
	if (txErrorShown == false)
	{
		txErrorShown = true;
		uiNotificationShowKind(UI_ALERT_ERROR, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2500U,
				((res == APRS_INFO_TX_NO_CONFIG) ? "APRS: no ch. config" : "APRS: TX not allowed"), true);
		soundSetMelody(MELODY_ERROR_BEEP);
	}
}

static bool messageAddressedToMe(const char *addressee, const char *own)
{
	return (strcmp(addressee, own) == 0);
}

static void handleMessage(const aprsRxFrame_t *frame, uint32_t now)
{
	aprsRxParsed_t p;
	aprsRxMessage_t msg;
	char own[APRS_RX_CALL_LEN + 4];

	// Replies from internet services (WXBOT, WTSAPP ...) come through an IGate as third-party frames
	if ((aprsRxParse(frame, &p) == false) || (aprsRxUnwrapThirdParty(&p) == false) || (aprsRxParseMessage(&p, &msg) == false))
	{
		return;
	}

	bool canTx = aprsGetOwnCallsign(own, sizeof(own));

	if (messageAddressedToMe(msg.addressee, own))
	{
		if (msg.isAck || msg.isRej)
		{
			for (uint32_t i = 0U; i < messageCount; i++)
			{
				if ((messages[i].kind == APRS_MSG_OUT_PENDING) && (strcmp(messages[i].peer, p.src) == 0) && (strcmp(messages[i].msgId, msg.msgId) == 0))
				{
					char buf[SCREEN_LINE_BUFFER_SIZE];

					messages[i].kind = (msg.isAck ? APRS_MSG_OUT_ACKED : APRS_MSG_OUT_REJECTED);
					snprintf(buf, sizeof(buf), "%s %s", (msg.isAck ? "ACK" : "REJ"), p.src);
#if defined(HAS_EVENT_LOG)
					eventLogAdd(EVENT_LOG_MSG_OUT, "%s", buf);
#endif
					uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, buf, false);
					soundSetMelody(msg.isAck ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
					break;
				}
			}
			return;
		}

		bool duplicate = false;
		if (msg.msgId[0] != 0)
		{
			for (uint32_t i = 0U; i < messageCount; i++)
			{
				if ((messages[i].kind == APRS_MSG_IN) && (strcmp(messages[i].peer, p.src) == 0) && (strcmp(messages[i].msgId, msg.msgId) == 0))
				{
					duplicate = true; // a retry: our ack was lost, ack again
					break;
				}
			}

			if (canTx)
			{
				queueAck(p.src, msg.msgId, now);
			}
		}

		if (duplicate == false)
		{
			char buf[SCREEN_LINE_BUFFER_SIZE];
			aprsRxMsgEntry_t *m = messageAdd();

			snprintf(m->peer, sizeof(m->peer), "%s", p.src);
			snprintf(m->text, sizeof(m->text), "%s", msg.text);
			snprintf(m->msgId, sizeof(m->msgId), "%s", msg.msgId);
			m->timeMs = now;
			m->kind = APRS_MSG_IN;
			m->unread = true;

			snprintf(buf, sizeof(buf), "%s:%s", p.src, msg.text);
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, MSG_NOTIFICATION_MS, buf, false);
#if defined(HAS_EVENT_LOG)
			eventLogAdd(EVENT_LOG_MSG_IN, "%s:%s", p.src, msg.text);
#endif
			soundSetMelody(MELODY_PRIVATE_CALL);
		}
	}
	else if ((strncmp(msg.addressee, "BLN", 3) == 0) && (msg.isAck == false) && (msg.isRej == false))
	{
		for (uint32_t i = 0U; i < messageCount; i++)
		{
			if ((messages[i].kind == APRS_MSG_IN_BULLETIN) && (strcmp(messages[i].peer, p.src) == 0) && (strcmp(messages[i].text, msg.text) == 0))
			{
				messages[i].timeMs = now; // repeated bulletin
				return;
			}
		}

		aprsRxMsgEntry_t *m = messageAdd();

		snprintf(m->peer, sizeof(m->peer), "%s", p.src);
		snprintf(m->text, sizeof(m->text), "%s", msg.text);
		m->timeMs = now;
		m->kind = APRS_MSG_IN_BULLETIN;
		m->unread = true;
	}
}

// One transmission at most per call (each one blocks the main loop for a while).
static void messagesTxTick(uint32_t now)
{
	char info[APRS_RX_MSG_TEXT_LEN + 24];
	aprsInfoTxResult_t res;

	int menu = menuSystemGetCurrentMenuNumber();

	if (aprsBeaconingIsTransmitting() || trxTransmissionEnabled ||
			(menu == UI_TX_SCREEN) || (menu == UI_HOTSPOT_MODE) || (menu == UI_CPS) || (menu == MENU_SATELLITE)
#if defined(HAS_SSTV_MENU)
			|| (menu == MENU_SSTV) // Fork: the SSTV screen may be on a downlink
#endif
			)
	{
		return;
	}

	// Acks first
	if ((ackCount > 0U) && ((int32_t)(now - ackQueue[0].dueMs) >= 0))
	{
		if ((ackQueue[0].tries == 0U) && (ackRateAllows(now) == false))
		{
			memmove(&ackQueue[0], &ackQueue[1], ((ACK_QUEUE_SIZE - 1U) * sizeof(ackPending_t)));
			ackCount--; // too many acks lately: dropped
			return;
		}

		snprintf(info, sizeof(info), ":%-9s:ack%s", ackQueue[0].to, ackQueue[0].msgId);
		res = sendInfo(info);

		if ((res == APRS_INFO_TX_SENT) || (res == APRS_INFO_TX_NO_CONFIG) || (res == APRS_INFO_TX_NOT_ALLOWED) || (++ackQueue[0].tries >= MSG_MAX_TRIES))
		{
			if ((res == APRS_INFO_TX_NO_CONFIG) || (res == APRS_INFO_TX_NOT_ALLOWED))
			{
				showTxError(res);
			}
			memmove(&ackQueue[0], &ackQueue[1], ((ACK_QUEUE_SIZE - 1U) * sizeof(ackPending_t)));
			ackCount--;
		}
		else
		{
			ackQueue[0].dueMs = (now + TX_BUSY_RETRY_MS);
		}
		return;
	}

	aprsRxMsgEntry_t *m = messagePendingOut();

	if ((m == NULL) || ((int32_t)(now - outNextTryMs) < 0))
	{
		return;
	}

	if (m->tries >= MSG_MAX_TRIES)
	{
		m->kind = APRS_MSG_OUT_FAILED;
		uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, "APRS msg: no ACK", false);
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	snprintf(info, sizeof(info), ":%-9s:%s{%s", m->peer, m->text, m->msgId);
	res = sendInfo(info);

	switch (res)
	{
		case APRS_INFO_TX_SENT:
			m->tries++;
			outNextTryMs = (now + MSG_RETRY_MS);
			break;
		case APRS_INFO_TX_NO_CONFIG:
		case APRS_INFO_TX_NOT_ALLOWED:
			m->kind = APRS_MSG_OUT_NOT_SENT;
			showTxError(res);
			break;
		default:
			outNextTryMs = (now + TX_BUSY_RETRY_MS);
			break;
	}
}

static void dtmfBackgroundTick(void)
{
	uint32_t head = dtmfHead;

	if ((head - dtmfSeen) > APRS_RX_TONE_TEXT_LEN)
	{
		dtmfSeen = (head - APRS_RX_TONE_TEXT_LEN);
	}

	while (dtmfSeen != head)
	{
		char c = dtmfRing[dtmfSeen % APRS_RX_TONE_TEXT_LEN];

		dtmfSeen++;
		if (c == ' ')
		{
			if (dtmfSequenceLen > 0U)
			{
				char buf[SCREEN_LINE_BUFFER_SIZE];
				int menu = menuSystemGetCurrentMenuNumber();

				dtmfSequence[dtmfSequenceLen] = 0;
#if defined(HAS_EVENT_LOG)
				eventLogAdd(EVENT_LOG_DTMF, "%s", dtmfSequence);
#endif
				if (backgroundOn && (monitorOn == false) && ((menu == UI_VFO_MODE) || (menu == UI_CHANNEL_MODE)))
				{
					snprintf(buf, sizeof(buf), "DTMF %s", dtmfSequence);
					uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, buf, false);
				}
				dtmfSequenceLen = 0U;
			}
		}
		else if (dtmfSequenceLen < (sizeof(dtmfSequence) - 1U))
		{
			dtmfSequence[dtmfSequenceLen++] = c;
		}
	}
}

void aprsRxTick(void)
{
	uint32_t now = ticksGetMillis();

	if (enabled == false)
	{
		return;
	}

#if defined(HAS_SSTV_MENU)
	// The SSTV decoder draws into the screen buffer: only while its screen is shown
	if (sstvOn && (menuSystemGetCurrentMenuNumber() != MENU_SSTV))
	{
		aprsRxSetSstv(false, NULL);
	}
#endif

	// Capture management
	if (captureAllowed() == false)
	{
		i2sAprsRxStop();
		setFMRxAudioCaptureActive(false);
		state = APRS_RX_STATE_PAUSED;
	}
	else if (i2sAprsRxIsActive())
	{
		setFMRxAudioCaptureActive(true);
		state = APRS_RX_STATE_LISTENING;
	}
	else if ((state != APRS_RX_STATE_NO_CLOCK) || ((now - lastNoClockTime) >= NO_CLOCK_RETRY_MS))
	{
		// Set the AT1846S to stock gain before starting I2S. The AT1846S is
		// upstream of the HR-C6000, so decoder input must not use the quiet
		// speaker-volume attenuation.
		setFMRxAudioCaptureActive(true);
		switch (i2sAprsRxStart())
		{
			case I2S_APRS_RX_STARTED:
			case I2S_APRS_RX_RUNNING:
				state = APRS_RX_STATE_LISTENING;
				break;
			case I2S_APRS_RX_NO_CLOCK:
				setFMRxAudioCaptureActive(false);
				state = APRS_RX_STATE_NO_CLOCK;
				lastNoClockTime = now;
				break;
			case I2S_APRS_RX_BUSY:
				setFMRxAudioCaptureActive(false);
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

		handleMessage(f, now);
	}

	dtmfBackgroundTick();
	stqcTick(now);

	messagesTxTick(now);
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

void aprsRxMessagesClear(void)
{
	messageCount = 0U;
	ackCount = 0U;
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
