/*
 * Fork: DMR SMS receive, send and inbox (see functions/dmrSms.h), by Marcin SP7TQ.
 * RX and TX paths ported from dondch/OpenGD77-AES256 (cleartext only).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <stdio.h>
#include "functions/dmrSms.h"

#if defined(HAS_DMR_SMS)

#include "hardware/SPI_Flash.h"
#include "functions/trx.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/eventLog.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/menuSystem.h"
#if defined(HAS_DMR_SMS_TX)
#include "hardware/HR-C6000.h"
#include "functions/codeplug.h"
#include "functions/settings.h"
#include "functions/aprs.h"
#include "functions/rxPowerSaving.h"
#include "interfaces/gps.h"
#include "interfaces/interrupts.h"
#endif

#define SECTOR_SIZE         4096U
#define PAGE_SIZE           256U
#define SLOTS_PER_SECTOR    (SECTOR_SIZE / PAGE_SIZE)                  // 16
#define REC_MAGIC           0x314D5344UL                              // "DSM1"
#define EMPTY_WORD          0xFFFFFFFFUL
#define NOTIFICATION_MS     5000U
#define DUP_WINDOW_MS       60000U                                     // a sender's TMS level repeat of the same text

// interfaces/gps.c: LOG_FLASH_16MB_START_ADDRESS + LOG_FLASH_16MB_MEM_SIZE
_Static_assert(((14U * 1024U * 1024U) + (64U * 1024U) + ((2U * 1024U * 1024U) - (128U * 1024U) - (320U * 1024U) - DMR_SMS_FLASH_SIZE)) == DMR_SMS_FLASH_ADDRESS,
		"the GPS log must end where the DMR SMS inbox starts");
_Static_assert((DMR_SMS_FLASH_ADDRESS % SECTOR_SIZE) == 0U, "DMR SMS inbox sector alignment");
_Static_assert((DMR_SMS_SLOTS * PAGE_SIZE) == DMR_SMS_FLASH_SIZE, "DMR SMS inbox size");

typedef struct __attribute__((__packed__))
{
	uint32_t magic;
	uint32_t seq;
	uint32_t time;
	uint32_t src;
	uint32_t dst;
	uint8_t  flags;
	uint8_t  len;
	uint8_t  reserved[2];
} dmrSmsRecordHeader_t;

typedef struct __attribute__((__packed__))
{
	dmrSmsRecordHeader_t h;
	char text[DMR_SMS_TEXT_MAX];
} dmrSmsRecord_t;

_Static_assert(sizeof(dmrSmsRecord_t) <= PAGE_SIZE, "DMR SMS record fits a flash page");

// Interrupt side: reassembly (about 760 bytes, the only large part)
static dmrSmsRx_t rx;

// Inbox index (main loop)
static bool     initialised = false;
static uint8_t  order[DMR_SMS_SLOTS];  // live slots, newest first
static uint8_t  liveCount;
static uint8_t  unreadCount;
static uint8_t  head;                  // next slot to write
static uint32_t nextSeq;
static uint32_t serial;

// Duplicate filter: an unacknowledged confirmed message comes again with the same IP id and
// sequence; the sender's last resort repeat has new ones but the same text
static bool     lastValid;
static uint32_t lastSrc;
static uint16_t lastIpId;
static uint8_t  lastSeq;
static uint8_t  lastFormat;
static uint32_t lastMs;

static uint32_t slotAddress(uint32_t slot)
{
	return (DMR_SMS_FLASH_ADDRESS + (slot * PAGE_SIZE));
}

static bool headerValid(const dmrSmsRecordHeader_t *h)
{
	return ((h->magic == REC_MAGIC) && (h->len <= DMR_SMS_TEXT_MAX));
}

// Reads every slot header: erases sectors that hold neither records nor erased flash (old GPS
// log data), finds the ring head and rebuilds the newest first index of the live messages
static void scan(void)
{
	dmrSmsRecordHeader_t h;
	uint32_t seqs[DMR_SMS_SLOTS];
	uint32_t newestSeq = 0U;
	int newest = -1;

	for (uint32_t sector = 0U; sector < (DMR_SMS_SLOTS / SLOTS_PER_SECTOR); sector++)
	{
		bool anyValid = false, anyDirty = false;

		for (uint32_t i = 0U; i < SLOTS_PER_SECTOR; i++)
		{
			SPI_Flash_read(slotAddress((sector * SLOTS_PER_SECTOR) + i), (uint8_t *)&h, sizeof(h));
			if (headerValid(&h))
			{
				anyValid = true;
			}
			else if (h.magic != EMPTY_WORD)
			{
				anyDirty = true;
			}
		}
		if (anyDirty && (anyValid == false))
		{
			SPI_Flash_eraseSector(slotAddress(sector * SLOTS_PER_SECTOR));
		}
	}

	liveCount = 0U;
	unreadCount = 0U;
	for (uint32_t slot = 0U; slot < DMR_SMS_SLOTS; slot++)
	{
		SPI_Flash_read(slotAddress(slot), (uint8_t *)&h, sizeof(h));
		seqs[slot] = 0U;
		if (headerValid(&h) == false)
		{
			continue;
		}
		if ((newest < 0) || (h.seq > newestSeq))
		{
			newestSeq = h.seq;
			newest = (int)slot;
		}
		if (h.flags & DMR_SMS_FLAG_LIVE)
		{
			// insertion into the newest first index
			uint32_t pos = liveCount;

			seqs[slot] = h.seq;
			while ((pos > 0U) && (seqs[order[pos - 1U]] < h.seq))
			{
				order[pos] = order[pos - 1U];
				pos--;
			}
			order[pos] = (uint8_t)slot;
			liveCount++;
			if (h.flags & DMR_SMS_FLAG_UNREAD)
			{
				unreadCount++;
			}
		}
	}

	if (newest < 0)
	{
		head = 0U;
		nextSeq = 1U;
	}
	else
	{
		head = (uint8_t)((newest + 1) % DMR_SMS_SLOTS);
		nextSeq = (newestSeq + 1U);
	}
	initialised = true;
}

static void ensureInit(void)
{
	if (initialised == false)
	{
		scan();
	}
}

// Files a message in the next slot (returned). 'flags': DMR_SMS_FLAG_* bits to set, the
// others of the byte are written cleared
static uint8_t store(uint32_t src, uint32_t dst, uint8_t flags, const char *text, int len)
{
	uint8_t page[PAGE_SIZE];
	dmrSmsRecord_t *rec = (dmrSmsRecord_t *)page;
	uint32_t addr = slotAddress(head);
	uint32_t word;

	ensureInit();
	SPI_Flash_read(addr, (uint8_t *)&word, sizeof(word));
	if (((head % SLOTS_PER_SECTOR) == 0U) || (word != EMPTY_WORD))
	{
		SPI_Flash_eraseSector(addr & ~(SECTOR_SIZE - 1U)); // the oldest messages were in it
	}

	memset(page, 0xFF, sizeof(page));
	rec->h.magic = REC_MAGIC;
	rec->h.seq = nextSeq++;
	rec->h.time = ((uiDataGlobal.dateTimeSecs > 86400U) ? (uint32_t)uiDataGlobal.dateTimeSecs : 0U);
	rec->h.src = src;
	rec->h.dst = dst;
	rec->h.flags = flags;
	rec->h.len = (uint8_t)len;
	memcpy(rec->text, text, (size_t)len);
	SPI_Flash_writePage(addr, page);

	uint8_t slot = head;
	head = (uint8_t)((head + 1U) % DMR_SMS_SLOTS);
	scan();
	serial++;
	return slot;
}

// Clears flag bits of a record in place (programming only turns bits to 0)
static void clearFlags(uint32_t slot, uint8_t bits)
{
	uint8_t page[PAGE_SIZE];
	dmrSmsRecord_t *rec = (dmrSmsRecord_t *)page;
	uint32_t addr = slotAddress(slot);

	SPI_Flash_read(addr, page, PAGE_SIZE);
	if (headerValid(&rec->h) && (rec->h.flags & bits))
	{
		rec->h.flags &= (uint8_t)~bits;
		SPI_Flash_writePage(addr, page);
	}
}

uint32_t dmrSmsCount(void)
{
	ensureInit();
	return liveCount;
}

uint32_t dmrSmsUnreadCount(void)
{
	ensureInit();
	return unreadCount;
}

uint32_t dmrSmsSerial(void)
{
	ensureInit();
	return serial;
}

bool dmrSmsGet(uint32_t indexFromNewest, dmrSmsMessage_t *msg)
{
	dmrSmsRecord_t rec;

	ensureInit();
	if (indexFromNewest >= liveCount)
	{
		return false;
	}

	SPI_Flash_read(slotAddress(order[indexFromNewest]), (uint8_t *)&rec, sizeof(rec));
	if (headerValid(&rec.h) == false)
	{
		return false;
	}

	msg->seq = rec.h.seq;
	msg->time = rec.h.time;
	msg->src = rec.h.src;
	msg->dst = rec.h.dst;
	msg->flags = rec.h.flags;
	msg->len = rec.h.len;
	memcpy(msg->text, rec.text, rec.h.len);
	msg->text[rec.h.len] = 0;
	return true;
}

void dmrSmsMarkRead(uint32_t indexFromNewest)
{
	dmrSmsRecordHeader_t h;

	ensureInit();
	if (indexFromNewest >= liveCount)
	{
		return;
	}
	SPI_Flash_read(slotAddress(order[indexFromNewest]), (uint8_t *)&h, sizeof(h));
	if (headerValid(&h) && (h.flags & DMR_SMS_FLAG_UNREAD))
	{
		clearFlags(order[indexFromNewest], DMR_SMS_FLAG_UNREAD);
		if (unreadCount > 0U)
		{
			unreadCount--;
		}
	}
}

void dmrSmsDelete(uint32_t indexFromNewest)
{
	ensureInit();
	if (indexFromNewest < liveCount)
	{
		clearFlags(order[indexFromNewest], DMR_SMS_FLAG_LIVE);
		scan();
		serial++;
	}
}

void dmrSmsDeleteAll(void)
{
	ensureInit();
	for (uint32_t s = 0U; s < (DMR_SMS_SLOTS / SLOTS_PER_SECTOR); s++)
	{
		SPI_Flash_eraseSector(DMR_SMS_FLASH_ADDRESS + (s * SECTOR_SIZE));
	}
	scan();
	serial++;
}

void dmrSmsPeerName(uint32_t id, char *buf, size_t size)
{
	char name[MAX_DMR_ID_CONTACT_TEXT_LENGTH];
	dmrIdDataStruct_t rec;

	id &= 0x00FFFFFFU;
	if (contactIDLookup(id, CONTACT_CALLTYPE_PC, name))
	{
		snprintf(buf, size, "%s", name);
	}
	else if (dmrIDLookup(id, &rec))
	{
		char call[SCREEN_LINE_BUFFER_SIZE];

		getCallsignOnly(call, rec.text);
		snprintf(buf, size, "%s", call);
	}
	else
	{
		snprintf(buf, size, "ID %u", (unsigned)id);
	}
}

// --- interrupt side ----------------------------------------------------------------------
volatile dmrSmsDiag_t dmrSmsDiag;

void dmrSmsIsrBurst(int dataType, const uint8_t *payload)
{
	uint8_t wasReady = rx.ready;

	switch (dataType)
	{
		case DMR_SMS_DT_DATA_HEADER:
			dmrSmsDiag.hdr++;
			memcpy((void *)dmrSmsDiag.lastHdr, payload, 12);
			break;
		case DMR_SMS_DT_RATE12_DATA:
			dmrSmsDiag.r12++;
			break;
		default:
			dmrSmsDiag.r34++;
			break;
	}
	dmrSmsRxFeed(&rx, dataType, payload, trxDMRID);
	if ((wasReady == 0U) && (rx.ready != 0U))
	{
		dmrSmsDiag.pduDone++;
	}
}

void dmrSmsIsrCallEnd(void)
{
	dmrSmsRxCallEnd(&rx);
}

bool dmrSmsIsrAckTake(uint8_t out[12])
{
	if (rx.ackReady == 0U)
	{
		return false;
	}
	memcpy(out, rx.ack, 12);
	rx.ackReady = 0U;
	return true;
}

// --- main loop ---------------------------------------------------------------------------
static bool isDuplicate(uint32_t src, const dmrSmsPduMeta_t *meta, const char *text, int len)
{
	uint32_t now = ticksGetMillis();
	bool dup = false;

	// a link layer repeat: same sender, format, IP id and sequence (short data has neither)
	if (lastValid && (meta->format != DMR_SMS_FMT_SHORT) && (src == lastSrc) && (meta->format == lastFormat) &&
			(meta->ipId == lastIpId) && (meta->seq == lastSeq))
	{
		return true;
	}

	// the same text from the same sender a moment ago
	if (lastValid && ((now - lastMs) < DUP_WINDOW_MS))
	{
		dmrSmsMessage_t last;

		if (dmrSmsGet(0U, &last) && (last.src == src) && (last.len == len) && (memcmp(last.text, text, (size_t)len) == 0))
		{
			dup = true; // its own link layer repeats are caught by the rule above from now on
		}
	}

	lastValid = true;
	lastSrc = src;
	lastIpId = meta->ipId;
	lastSeq = meta->seq;
	lastFormat = meta->format;
	if (dup == false)
	{
		lastMs = now;
	}
	return dup;
}

#if defined(HAS_DMR_SMS_TX)
static void txTick(void);
#endif

void dmrSmsTick(void)
{
	char text[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;
	dmrSmsPduInfo_t info;
	int len;

#if defined(HAS_DMR_SMS_TX)
	txTick();
#endif
	if (rx.ready == 0U)
	{
		return;
	}

	info = rx.info;
	len = dmrSmsPduDecode(rx.pdu, &info, text, sizeof(text), &meta);
	rx.ready = 0U; // pdu[] back to the interrupt

	if (len <= 0)
	{
		dmrSmsDiag.decodeFail++;
		return;
	}
	if (dmrSmsPduForUs(&info, trxDMRID) == false)
	{
		dmrSmsDiag.notUs++;
		return;
	}
	if (isDuplicate(info.src, &meta, text, len))
	{
		dmrSmsDiag.dup++;
		return;
	}
	dmrSmsDiag.stored++;

	// a received message: LIVE, UNREAD, RECEIVED and the unused bits set (as the first records)
	store(info.src, info.dst, (uint8_t)(DMR_SMS_FLAG_LIVE | DMR_SMS_FLAG_UNREAD | ((info.group != 0U) ? DMR_SMS_FLAG_GROUP : 0U) | 0xF8U), text, len);

	char peer[SCREEN_LINE_BUFFER_SIZE];
	char note[48];

	dmrSmsPeerName(info.src, peer, sizeof(peer));
	snprintf(note, sizeof(note), "SMS %s:%s", peer, text);
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, note, false);
#if defined(HAS_EVENT_LOG)
	eventLogAdd(EVENT_LOG_MSG_IN, "%u:%s", (unsigned)info.src, text);
#endif
	soundSetMelody(MELODY_PRIVATE_CALL);
}

#if defined(HAS_DMR_SMS_TX)
// --- send --------------------------------------------------------------------------------
// TX path ported from dondch/OpenGD77-AES256 (cleartext only): the burst sequence and the way the
// data call is keyed and ended follow their functions/dmr_data.c (dmrDataTxLoad, dmrDataKeyTx,
// dmrDataTxFinishPoll) and dmr_sms.c (dmrSmsSend). The PDU is built into rx.pdu (lent), the
// CSBK preamble is made burst by burst in the interrupt: about 40 bytes of RAM in all.
#define TX_TIMEOUT_MS      6000U   // keyed to the end of the last block (34 bursts: about 2 s, plus a repeater wake up)
#define TX_END_WAIT_MS     1500U   // for the HR-C6000 to finish the call after the last burst / an abort
#define TX_GRACE_MS        200U    // then before the RF chip is set back to receive (the original's Response window)
#define SLOT_TYPE_CSBK     0x30U   // HR-C6000 reg 0x50: slot type << 4
#define SLOT_TYPE_HEADER   0x60U
#define SLOT_TYPE_RATE12   0x70U

enum { TXS_IDLE = 0, TXS_RUN, TXS_DRAINED, TXS_ABORT };

static struct
{
	volatile uint8_t state;
	volatile uint8_t next;     // bursts handed to the interrupt
	uint8_t  total;            // preamble + header + blocks
	uint8_t  group;
	uint8_t  slot;             // inbox slot of the message
	uint8_t  result;
	uint8_t  hdr[12];
	uint16_t counter;          // IP id / TMS sequence: a stock TYT drops a repeat of the last pair
	uint32_t dst;
	uint32_t startMs;
	uint32_t quietMs;          // the call has ended (0: not yet)
} tx;

static dmrSmsTxResult_t txCheck(void)
{
	if ((currentChannelData == NULL) || (trxGetMode() != RADIO_MODE_DIGITAL))
	{
		return DMR_SMS_TX_NOT_DMR; // on FM trxSetTX() would key the analog PA, with no DMR state machine to end it
	}
	if ((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) || (currentChannelData->txFreq == 0U))
	{
		return DMR_SMS_TX_RX_ONLY;
	}
	if (settingsIsOptionBitSet(BIT_TX_INHIBIT))
	{
		return DMR_SMS_TX_INHIBITED;
	}
	if ((nonVolatileSettings.txFreqLimited != BAND_LIMITS_NONE) && (trxCheckFrequencyInAmateurBand(currentChannelData->txFreq) == false))
	{
		return DMR_SMS_TX_OUT_OF_BAND;
	}
	if ((trxDMRID == 0U) || (trxDMRID > 0x00FFFFFFU))
	{
		return DMR_SMS_TX_NO_ID;
	}
	// not over a transmission, a call being heard, the scan, a hotspot session or an acknowledgement
	if ((tx.state != TXS_IDLE) || trxTransmissionEnabled || trxIsTransmitting || aprsBeaconingIsTransmitting() ||
			(settingsUsbMode == USB_MODE_HOTSPOT) || uiDataGlobal.Scan.active || (slotState >= DMR_STATE_TX_START_1) ||
			((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) != 0U))
	{
		return DMR_SMS_TX_BUSY;
	}
	return DMR_SMS_TX_OK;
}

dmrSmsTxResult_t dmrSmsSend(const char *text, uint32_t dst, bool group)
{
	dmrSmsTxResult_t r = txCheck();
	int len = (int)strlen(text);

	if (r != DMR_SMS_TX_OK)
	{
		return r;
	}
	if ((len == 0) || (dst == 0U) || (dst > 0x00FFFFFFU))
	{
		return DMR_SMS_TX_BAD_TEXT;
	}
	if (len > (int)DMR_SMS_TEXT_MAX)
	{
		len = (int)DMR_SMS_TEXT_MAX;
	}
	if (dmrSmsRxHold(&rx) == false)
	{
		return DMR_SMS_TX_BUSY; // a PDU is coming in, or waits for dmrSmsTick()
	}

	if (tx.counter == 0U)
	{
		tx.counter = (uint16_t)ticksGetMillis(); // not the same pairs again after every power up
	}
	tx.counter++;

	int blocks = dmrSmsTxBuild(rx.pdu, (int)sizeof(rx.pdu), tx.hdr, text, trxDMRID, dst, group, tx.counter, (uint8_t)(0x90U | (tx.counter & 0x0FU)));
	if (blocks <= 0)
	{
		dmrSmsRxRelease(&rx);
		return DMR_SMS_TX_BAD_TEXT;
	}

	// Filed first (outgoing, UNSENT until it went out): the flash write blocks, never inside the call
	tx.slot = store(trxDMRID, dst, (uint8_t)(DMR_SMS_FLAG_LIVE | DMR_SMS_FLAG_UNSENT | (group ? DMR_SMS_FLAG_GROUP : 0U) | 0xE0U), text, len);

	tx.dst = dst;
	tx.group = (group ? 1U : 0U);
	tx.total = (uint8_t)(DMR_SMS_TX_PREAMBLE + 1U + (uint32_t)blocks);
	tx.next = 0U;
	tx.quietMs = 0U;
	tx.result = DMR_SMS_TX_SENDING;
	tx.startMs = ticksGetMillis();

	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF)
	{
		gpsDataInputStartStop(false); // as the TX screen does
	}
#endif
	// hrc6000Tick() keys a call only from the idle slot state, which a recent reception keeps away
	// for a while (the original's bench): forced, as in their dmrDataKeyTx()
	HRC6000ForceDMRIdleForTx();
	tx.state = TXS_RUN;
	trxEnableTransmission(); // LEDs, PA drive for the TX frequency, trxTransmissionEnabled
	return DMR_SMS_TX_OK;
}

static void txAbort(dmrSmsTxResult_t r)
{
	// unless the interrupt has just served the last burst (it ends the call the same way)
	interruptsDisableC6000Interrupts();
	if (tx.state == TXS_RUN)
	{
		tx.state = TXS_ABORT; // the interrupt serves no more bursts
		tx.result = r;
		trxTransmissionEnabled = false;
	}
	interruptsEnableC6000Interrupts();
}

static void txTick(void)
{
	uint32_t now = ticksGetMillis();

	if (tx.state == TXS_IDLE)
	{
		return;
	}

	if (tx.state == TXS_RUN)
	{
		if (HRC6000GetIsWakingState() == WAKING_MODE_FAILED)
		{
			txAbort(DMR_SMS_TX_NO_REPEATER);
		}
		else if (((now - tx.startMs) > TX_TIMEOUT_MS) || (trxTransmissionEnabled == false))
		{
			txAbort(DMR_SMS_TX_TIMEOUT); // never started (slot busy), or something else ended it
		}
		else
		{
			return;
		}
	}

	// the last burst is out (or the call was given up): wait for the end of the call, then the grace
	if (tx.quietMs == 0U)
	{
		if (trxIsTransmitting && ((now - tx.startMs) < (TX_TIMEOUT_MS + TX_END_WAIT_MS)))
		{
			return;
		}
		tx.quietMs = (now | 1U);
		return;
	}
	if ((now - tx.quietMs) < TX_GRACE_MS)
	{
		return;
	}

	// as a PTT release: red LED off, RF back to receive
	tx.state = TXS_IDLE;
	trxTransmissionEnabled = false;
	trxDisableTransmission();
	HRC6000ClearIsWakingState();
#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF)
	{
		gpsDataInputStartStop(true);
	}
#endif
	dmrSmsRxRelease(&rx);

	if (tx.result == DMR_SMS_TX_SENDING)
	{
		dmrSmsRecord_t rec;

		tx.result = DMR_SMS_TX_OK;
		clearFlags(tx.slot, DMR_SMS_FLAG_UNSENT);
		SPI_Flash_read(slotAddress(tx.slot), (uint8_t *)&rec, sizeof(rec));
#if defined(HAS_EVENT_LOG)
		if (headerValid(&rec.h))
		{
			eventLogAdd(EVENT_LOG_MSG_OUT, "%u:%.*s", (unsigned)tx.dst, (int)rec.h.len, rec.text);
		}
#endif
	}
	serial++; // the screen redraws the message with its new state
}

dmrSmsTxResult_t dmrSmsTxResult(void)
{
	return (dmrSmsTxResult_t)tx.result;
}

bool dmrSmsTxBusy(void)
{
	return (tx.state != TXS_IDLE);
}

const char *dmrSmsTxResultText(dmrSmsTxResult_t r)
{
	static const char *texts[] = { "SENT", "SENDING", "NOT A DMR CH", "RX ONLY CHANNEL", "TX INHIBITED", "OUT OF BAND",
			"NO DMR ID", "CHANNEL BUSY", "NO TEXT / DEST", "NO REPEATER", "NOT SENT" };

	return (((unsigned)r < (sizeof(texts) / sizeof(texts[0]))) ? texts[r] : "FAILED");
}

// --- HR-C6000 timeslot interrupt ---
bool dmrSmsIsrTxActive(void)
{
	return (tx.state == TXS_RUN);
}

bool dmrSmsIsrTxNext(uint8_t *slotType, uint8_t payload[12])
{
	uint8_t k = tx.next;

	if (tx.state != TXS_RUN)
	{
		return false;
	}
	if (k >= tx.total)
	{
		tx.state = TXS_DRAINED;
		trxTransmissionEnabled = false;
		return false;
	}

	if (k < DMR_SMS_TX_PREAMBLE)
	{
		dmrSmsTxPreamble(payload, trxDMRID, tx.dst, (tx.group != 0U), (uint8_t)(tx.total - 1U - k));
		*slotType = SLOT_TYPE_CSBK;
	}
	else if (k == DMR_SMS_TX_PREAMBLE)
	{
		memcpy(payload, tx.hdr, 12);
		*slotType = SLOT_TYPE_HEADER;
	}
	else
	{
		memcpy(payload, &rx.pdu[(k - DMR_SMS_TX_PREAMBLE - 1U) * DMR_SMS_TX_BLOCK_LEN], DMR_SMS_TX_BLOCK_LEN);
		*slotType = SLOT_TYPE_RATE12;
	}
	tx.next = (uint8_t)(k + 1U);
	return true;
}
#endif

#endif
