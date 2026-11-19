/*
 * Fork: EMCOM event log (station log), by Marcin SP7TQ. See eventLog.h.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "functions/eventLog.h"

#if defined(HAS_EVENT_LOG)

#include "hardware/SPI_Flash.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "functions/trx.h"
#include "functions/sound.h"
#include "functions/aprs.h"
#include "functions/ticks.h"

#define REC_SIZE          32U
#define RECS              (EVENT_LOG_FLASH_SIZE / REC_SIZE)   // 2048
#define SECTOR_SIZE       4096U
#define RECS_PER_SECTOR   (SECTOR_SIZE / REC_SIZE)           // 128
#define SECTORS           (EVENT_LOG_FLASH_SIZE / SECTOR_SIZE)
#define PAGE_SIZE         256U
#define EMPTY_SEQ         0xFFFFFFFFU
#define RX_MIN_MS         2000U
#define DMR_NEW_CALL_MS   10000U

static bool     initialised = false;
static uint32_t head;      // next record slot
static uint32_t nextSeq;
static uint32_t count;

static uint32_t recordAddress(uint32_t slot)
{
	return (EVENT_LOG_FLASH_ADDRESS + (slot * REC_SIZE));
}

static uint32_t readSeq(uint32_t slot)
{
	uint32_t seq = EMPTY_SEQ;

	SPI_Flash_read(recordAddress(slot), (uint8_t *)&seq, sizeof(seq));
	return seq;
}

// Finds the newest record (the sector whose first record has the highest sequence number)
static void init(void)
{
	uint32_t newestSector = 0U, newestFirst = 0U, oldestFirst = EMPTY_SEQ;
	bool any = false;

	for (uint32_t s = 0U; s < SECTORS; s++)
	{
		uint32_t seq = readSeq(s * RECS_PER_SECTOR);

		if (seq != EMPTY_SEQ)
		{
			if ((any == false) || (seq > newestFirst))
			{
				newestFirst = seq;
				newestSector = s;
			}
			if (seq < oldestFirst)
			{
				oldestFirst = seq;
			}
			any = true;
		}
	}

	if (any == false)
	{
		head = 0U;
		nextSeq = 1U;
		count = 0U;
	}
	else
	{
		uint32_t i = 0U;
		uint32_t lastSeq = newestFirst;

		while (i < RECS_PER_SECTOR)
		{
			uint32_t seq = readSeq((newestSector * RECS_PER_SECTOR) + i);

			if (seq == EMPTY_SEQ)
			{
				break;
			}
			lastSeq = seq;
			i++;
		}

		head = (((newestSector * RECS_PER_SECTOR) + i) % RECS);
		nextSeq = (lastSeq + 1U);
		count = (nextSeq - oldestFirst);
		if (count > RECS)
		{
			count = RECS;
		}
	}

	initialised = true;
}

static void writeRecord(const eventLogRecord_t *rec)
{
	uint8_t page[PAGE_SIZE];
	uint32_t addr = recordAddress(head);
	uint32_t pageAddr = (addr & ~(PAGE_SIZE - 1U));

	if ((head % RECS_PER_SECTOR) == 0U)
	{
		SPI_Flash_eraseSector(addr);
		if (count > (RECS - RECS_PER_SECTOR))
		{
			count = (RECS - RECS_PER_SECTOR); // the oldest records were in that sector
		}
	}

	// Page program only: the other records of the page are written back unchanged
	SPI_Flash_read(pageAddr, page, PAGE_SIZE);
	memcpy(&page[addr - pageAddr], rec, sizeof(eventLogRecord_t));
	SPI_Flash_writePage(pageAddr, page);

	head = ((head + 1U) % RECS);
	count++;
}

void eventLogAdd(eventLogType_t type, const char *fmt, ...)
{
	eventLogRecord_t rec;
	va_list args;

	if (initialised == false)
	{
		init();
	}

	memset(&rec, 0, sizeof(rec));
	rec.seq = nextSeq++;
	rec.time = ((uiDataGlobal.dateTimeSecs > 86400U) ? uiDataGlobal.dateTimeSecs : 0U);
	rec.type = (uint8_t)type;

	va_start(args, fmt);
	vsnprintf(rec.text, sizeof(rec.text), fmt, args);
	va_end(args);

	writeRecord(&rec);
}

uint32_t eventLogCount(void)
{
	if (initialised == false)
	{
		init();
	}
	return count;
}

uint32_t eventLogSerial(void)
{
	if (initialised == false)
	{
		init();
	}
	return nextSeq;
}

bool eventLogGet(uint32_t indexFromNewest, eventLogRecord_t *rec)
{
	if (initialised == false)
	{
		init();
	}

	if (indexFromNewest >= count)
	{
		return false;
	}

	uint32_t slot = (((head + RECS) - 1U - indexFromNewest) % RECS);

	SPI_Flash_read(recordAddress(slot), (uint8_t *)rec, sizeof(eventLogRecord_t));
	rec->text[EVENT_LOG_TEXT_LEN - 1U] = 0;
	return (rec->seq != EMPTY_SEQ);
}

void eventLogClear(void)
{
	for (uint32_t s = 0U; s < SECTORS; s++)
	{
		SPI_Flash_eraseSector(EVENT_LOG_FLASH_ADDRESS + (s * SECTOR_SIZE));
	}
	head = 0U;
	count = 0U;
	initialised = true; // keep nextSeq increasing
}

const char *eventLogTypeName(uint8_t type)
{
	static const char *names[EVENT_LOG_TYPE_MAX] = { "SYS", "TX", "RX", "DMR", "APR", "MS<", "MS>", "DTM", "SOS", "SST" };

	return ((type < EVENT_LOG_TYPE_MAX) ? names[type] : "?");
}

// --- automatic events ------------------------------------------------------------------
static void formatFreq(uint32_t freq, char *buf, size_t size)
{
	snprintf(buf, size, "%u.%04u", (unsigned)(freq / 100000U), (unsigned)((freq % 100000U) / 10U));
}

void eventLogTick(void)
{
	static bool txOn = false, rxOn = false;
	static uint32_t txStart, rxStart, txFreq, rxFreq;
	static int16_t rxPeak;
	static uint32_t dmrLastId = 0U, dmrLastSeen = 0U;
	static bool dmrPrimed = false;
	uint32_t now = ticksGetMillis();
	char f[12];

	// Own transmissions (voice, not the APRS beacons)
	bool tx = (trxTransmissionEnabled && (aprsBeaconingIsTransmitting() == false));
	if (tx != txOn)
	{
		if (tx)
		{
			txStart = now;
			txFreq = currentChannelData->txFreq;
		}
		else
		{
			formatFreq(txFreq, f, sizeof(f));
			eventLogAdd(EVENT_LOG_TX, "%s %us", f, (unsigned)((now - txStart + 500U) / 1000U));
		}
		txOn = tx;
	}

	// FM receptions of 2 s or more, with the peak level
	bool rx = ((tx == false) && (trxGetMode() == RADIO_MODE_ANALOG) && ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) != 0));
	if (rx)
	{
		int16_t dBm = (int16_t)trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

		if (rxOn == false)
		{
			rxStart = now;
			rxFreq = currentChannelData->rxFreq;
			rxPeak = dBm;
		}
		else if (dBm > rxPeak)
		{
			rxPeak = dBm;
		}
	}
	else if (rxOn && ((now - rxStart) >= RX_MIN_MS))
	{
		formatFreq(rxFreq, f, sizeof(f));
		eventLogAdd(EVENT_LOG_RX, "%s %us %d", f, (unsigned)((now - rxStart + 500U) / 1000U), (int)rxPeak);
	}
	rxOn = rx;

	// DMR calls: a new caller, or the same one after a pause
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (uiDataGlobal.lastHeardCount > 0) && (LinkHead != NULL) && (LinkHead->id != 0U))
	{
		if (dmrPrimed == false) // an older call already in the list is not a new event
		{
			dmrPrimed = true;
		}
		else if ((LinkHead->id != dmrLastId) || ((LinkHead->time - dmrLastSeen) > DMR_NEW_CALL_MS))
		{
			char call[SCREEN_LINE_BUFFER_SIZE];
			const char *src = (((LinkHead->talkerAlias[0] != 0) && (strncmp(LinkHead->contact, "ID:", 3) == 0)) ? LinkHead->talkerAlias : LinkHead->contact);

			if ((LinkHead->time != dmrLastSeen) || (LinkHead->id != dmrLastId))
			{
				getCallsignOnly(call, src);
				call[10] = 0;
				eventLogAdd(EVENT_LOG_DMR, "%s TG%u", call, (unsigned)(LinkHead->talkGroupOrPcId & 0xFFFFFFU));
			}
		}
		dmrLastId = LinkHead->id;
		dmrLastSeen = LinkHead->time;
	}
}

#endif // HAS_EVENT_LOG
