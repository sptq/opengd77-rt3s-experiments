/*
 * Fork: DMR SMS receive and inbox, by Marcin SP7TQ. RX path ported from
 * dondch/OpenGD77-AES256 (cleartext only); the PDU / text logic is functions/dmrSmsPdu.c.
 *
 * Interrupt side: the HR-C6000 RX interrupt hands every CRC valid data burst (header, rate 1/2,
 * rate 3/4) to dmrSmsIsrBurst(); a completed PDU waits for dmrSmsTick() (main loop), which
 * decodes it, files it in the inbox (SPI flash), logs it and notifies. Received cleartext only;
 * an encrypted PDU is dropped.
 *
 * Send (HAS_DMR_SMS_TX, TX path ported from dondch/OpenGD77-AES256, cleartext only):
 * dmrSmsSend() checks the gates of a PTT press, files the message (outgoing, not yet sent),
 * builds the PDU into the receive buffer (lent, dmrSmsRxHold) and keys a data call on the
 * current DMR channel (its frequency, colour code and timeslot); the HR-C6000 timeslot
 * interrupt takes the bursts from dmrSmsIsrTxNext() instead of the Voice LC headers and the
 * voice frames; dmrSmsTick() ends the call and marks the message sent.
 *
 * Inbox: 32 slots of one flash page (256 bytes) in two 4 KB sectors at DMR_SMS_FLASH_ADDRESS,
 * a ring: entering a sector erases it, so the newest 16..32 messages are kept. Read / deleted
 * marks clear bits of the record in place (no erase).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_DMR_SMS_H_
#define _OPENGD77_DMR_SMS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "functions/dmrSmsPdu.h"

#if defined(HAS_DMR_SMS)
#include "functions/sstvStore.h"

// SPI flash (16 MB) use from 14 MB up: event log 64 KB (functions/eventLog.h), GPS NMEA log
// (interfaces/gps.c), this inbox 8 KB, SSTV pictures 320 KB (functions/sstvStore.h), field
// tools data 64 KB (functions/fieldData.h). The inbox was taken from the end of the GPS log.
#define DMR_SMS_FLASH_SIZE      (8U * 1024U)
#define DMR_SMS_FLASH_ADDRESS   (SSTV_STORE_FLASH_ADDRESS - DMR_SMS_FLASH_SIZE) // 0xF9E000, the GPS log ends here
#define DMR_SMS_SLOTS           32U

#define DMR_SMS_FLAG_LIVE       0x01U  // cleared: deleted
#define DMR_SMS_FLAG_UNREAD     0x02U  // cleared: read
#define DMR_SMS_FLAG_GROUP      0x04U  // dst is a talkgroup
#define DMR_SMS_FLAG_RECEIVED   0x08U  // cleared: a message we sent (src is our ID); older records have it set
#define DMR_SMS_FLAG_UNSENT     0x10U  // a message we sent: cleared once it went out, set: it failed
#define DMR_SMS_IS_OUTGOING(f)  (((f) & DMR_SMS_FLAG_RECEIVED) == 0U)

typedef struct
{
	uint32_t seq;
	uint32_t time;    // UTC seconds, 0: no clock
	uint32_t src;
	uint32_t dst;
	uint8_t  flags;
	uint8_t  len;
	char     text[DMR_SMS_TEXT_MAX + 1];
} dmrSmsMessage_t;

// main loop
void dmrSmsTick(void);
uint32_t dmrSmsCount(void);
uint32_t dmrSmsUnreadCount(void);
uint32_t dmrSmsSerial(void);                           // changes when a message arrives or goes
bool dmrSmsGet(uint32_t indexFromNewest, dmrSmsMessage_t *msg);
void dmrSmsMarkRead(uint32_t indexFromNewest);
void dmrSmsDelete(uint32_t indexFromNewest);
void dmrSmsDeleteAll(void);
void dmrSmsPeerName(uint32_t id, char *buf, size_t size); // contact / DMR ID database callsign, else "ID n"

#if defined(HAS_DMR_SMS_TX)
typedef enum
{
	DMR_SMS_TX_OK = 0,        // keyed (dmrSmsSend) / went out (dmrSmsTxResult)
	DMR_SMS_TX_SENDING,
	DMR_SMS_TX_NOT_DMR,       // FM channel
	DMR_SMS_TX_RX_ONLY,
	DMR_SMS_TX_INHIBITED,
	DMR_SMS_TX_OUT_OF_BAND,
	DMR_SMS_TX_NO_ID,
	DMR_SMS_TX_BUSY,          // transmitting, receiving, scanning, hotspot mode, a PDU coming in
	DMR_SMS_TX_BAD_TEXT,
	DMR_SMS_TX_NO_REPEATER,   // the repeater did not wake up
	DMR_SMS_TX_TIMEOUT
} dmrSmsTxResult_t;

dmrSmsTxResult_t dmrSmsSend(const char *text, uint32_t dst, bool group);
dmrSmsTxResult_t dmrSmsTxResult(void);                 // of the last dmrSmsSend: SENDING until it ended
bool dmrSmsTxBusy(void);                               // a data call is keyed or ending
const char *dmrSmsTxResultText(dmrSmsTxResult_t r);    // at most 16 characters

// HR-C6000 timeslot interrupt
bool dmrSmsIsrTxActive(void);
// The next burst: slot type byte for reg 0x50 (0x30 CSBK, 0x60 data header, 0x70 rate 1/2) and
// 12 info bytes. False when all went out: the call is then over (trxTransmissionEnabled cleared).
bool dmrSmsIsrTxNext(uint8_t *slotType, uint8_t payload[12]);
#endif

// HR-C6000 interrupt
void dmrSmsIsrBurst(int dataType, const uint8_t *payload);

// Fork: receive diagnostics (DMR SMS screen, '*'): where a received data call gets lost
typedef struct
{
	uint16_t dataInt;    // data sync class bursts seen by the HR-C6000 interrupt
	uint16_t crcBad;     // ... of them with a bad burst CRC
	uint16_t hdr;        // data headers read (type 6)
	uint16_t r12;        // rate 1/2 blocks (type 7)
	uint16_t r34;        // rate 3/4 blocks (type 8)
	uint16_t other;      // other data types (CSBK 3, terminator 2, ...)
	uint16_t skipped;    // type 6/7/8 not read (own TX, hotspot mode, read error)
	uint16_t pduDone;    // PDUs completed (CRC32 good)
	uint16_t decodeFail; // completed but no text decoded
	uint16_t notUs;      // decoded, not to our ID / not a group
	uint16_t dup;        // repeats dropped
	uint16_t stored;     // messages filed
	uint16_t infoInt;    // HR-C6000 "information received" interrupts
	uint8_t  last51;     // last reg 0x51 of a data burst
	uint8_t  last90;     // last reg 0x90 of an information interrupt
	uint8_t  lastHdr[12];// last data header
} dmrSmsDiag_t;
extern volatile dmrSmsDiag_t dmrSmsDiag;
void dmrSmsIsrCallEnd(void);                           // CRC valid Terminator
bool dmrSmsIsrAckTake(uint8_t out[12]);                // the Response header owed for the PDU just completed

#endif
#endif
