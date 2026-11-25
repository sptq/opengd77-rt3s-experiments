/*
 * Fork: EMCOM event log (station log), by Marcin SP7TQ.
 *
 * Every transmission, FM reception, DMR call, new APRS station, APRS
 * message, DTMF sequence, SOS, SSTV picture, logged QSO and net control event
 * (net start / check-in / silence alarm / end) is written with its UTC time
 * into a 64 KB ring in the SPI flash (2048 records of 32 bytes), so it
 * survives a power cycle. Shown by main menu > Event log, exported with
 * tools/opengd77_event_log.py.
 *
 * Records are appended with a page program only (the sector is erased when
 * the ring enters it), no RAM buffer is needed.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_EVENT_LOG_H_
#define _OPENGD77_EVENT_LOG_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD9600)
#define HAS_EVENT_LOG

#define EVENT_LOG_FLASH_ADDRESS   (14U * 1024U * 1024U)  // the GPS NMEA log now starts 64 KB later
#define EVENT_LOG_FLASH_SIZE      (64U * 1024U)
#define EVENT_LOG_TEXT_LEN        20U

typedef enum
{
	EVENT_LOG_SYS = 0,
	EVENT_LOG_TX,
	EVENT_LOG_RX,
	EVENT_LOG_DMR,
	EVENT_LOG_APRS,
	EVENT_LOG_MSG_IN,
	EVENT_LOG_MSG_OUT,
	EVENT_LOG_DTMF,
	EVENT_LOG_SOS,
	EVENT_LOG_SSTV,
	EVENT_LOG_QSO = 10,                   // "<station> <report>[ OK|RPT|NIL]"
	EVENT_LOG_NET = 11,                   // "START[ <name>]", "IN <station>", "QUIET <station>", "END <n>"
	EVENT_LOG_TYPE_MAX
} eventLogType_t;

typedef struct __attribute__((packed))
{
	uint32_t seq;                         // 0xFFFFFFFF: empty
	uint32_t time;                        // UTC seconds, 0 = clock not set
	uint8_t  type;                        // eventLogType_t
	uint8_t  reserved[3];
	char     text[EVENT_LOG_TEXT_LEN];    // NUL terminated when shorter
} eventLogRecord_t;                       // 32 bytes

void eventLogAdd(eventLogType_t type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
uint32_t eventLogCount(void);
uint32_t eventLogSerial(void);              // changes with every new record
bool eventLogGet(uint32_t indexFromNewest, eventLogRecord_t *rec);
void eventLogClear(void);
const char *eventLogTypeName(uint8_t type); // 3 letters
void eventLogTick(void);                    // main loop: TX / FM RX / DMR call detection

#endif
#endif
