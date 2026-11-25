/*
 * Fork: DMR SMS receive and send, the hardware independent part (data PDU reassembly, text
 * decoding, the cleartext PDU builder), by Marcin SP7TQ. RX and TX paths ported from dondch/OpenGD77-AES256 (cleartext only): every
 * encryption, key, provisioning and diagnostic path of the original is left out. A PDU sent
 * under a Motorola ENC header is dropped without being read or acknowledged.
 *
 * ETSI TS 102 361-1 data PDUs as a stock TYT (MD-UV380 / MD-UV390) and the networks send them:
 *   - data header (slot type 6): Unconfirmed (DPF 2), Confirmed (DPF 3), Defined Short Data
 *     (DPF 13) or Raw Short Data (DPF 14); the HR-C6000 checks its CRC.
 *   - data blocks: rate 1/2 (slot type 7, 12 bytes) or rate 3/4 (slot type 8, 18 bytes). A
 *     Confirmed block leads with a 2 byte DBSN + CRC-9 field (10 / 16 payload bytes).
 *   - the payloads, concatenated, end with the packet CRC32: that CRC decides when the PDU is
 *     complete (the block count in the header is not trusted).
 * Text formats:
 *   - IPv4 / UDP / TMS (port 4007), UTF-16LE text: the stock "IP" SMS (private: Confirmed
 *     rate 3/4 blocks with response requested, group: Unconfirmed rate 1/2).
 *   - the stock "DMR format": 5 byte prefix [seq] 00 [00 private | 02 group] 01 01, then
 *     UTF-16BE text, the header's pad octets, the CRC32.
 *   - Defined / Raw Short Data (networks, gateways): the text in the header's DD format.
 * Non ASCII letters are mapped to plain ASCII (Polish letters to their base letter).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_DMR_SMS_PDU_H_
#define _OPENGD77_DMR_SMS_PDU_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD9600)
#define HAS_DMR_SMS
#define HAS_DMR_SMS_ACK   // acknowledge confirmed private SMS (hardware/HR-C6000.c), remove to receive silently
#define HAS_DMR_SMS_TX    // send (functions/dmrSms.c, hardware/HR-C6000.c, the inbox screen), remove for receive only
#endif

#define DMR_SMS_TEXT_MAX      140U   // characters kept of a message
#define DMR_SMS_PDU_MAX       352U   // reassembly buffer: a 144 character IP SMS (330 bytes) in any block size

// HR-C6000 slot data types (reg 0x51 bits 7..4)
#define DMR_SMS_DT_DATA_HEADER   6
#define DMR_SMS_DT_RATE12_DATA   7
#define DMR_SMS_DT_RATE34_DATA   8

#define DMR_SMS_FMT_IP     0U
#define DMR_SMS_FMT_SHORT  1U
#define DMR_SMS_FMT_TYT    2U

// What the main loop needs to know about a completed PDU
typedef struct
{
	uint16_t len;        // bytes in pdu[], the CRC32 included
	uint32_t src;
	uint32_t dst;
	uint8_t  group;      // dst is a talkgroup
	uint8_t  defined;    // Defined / Raw Short Data
	uint8_t  ddFormat;   // its DD format
	uint8_t  padBytes;   // pad octets before the CRC32 (short data: bit padding / 8)
} dmrSmsPduInfo_t;

// Reassembly state. Fed from the HR-C6000 interrupt; pdu[] belongs to the main loop while
// 'ready' is set (the interrupt does not touch it then).
typedef struct
{
	uint8_t  acc[DMR_SMS_PDU_MAX];   // the payloads of the PDU being received
	uint8_t  pdu[DMR_SMS_PDU_MAX];   // a completed PDU (ready), else the kept copy of a failed one
	dmrSmsPduInfo_t info;            // of pdu[] when ready
	volatile uint8_t ready;
	volatile uint8_t ackReady;
	volatile uint8_t hold;           // pdu[] is lent to a transmission (dmrSmsRxHold): nothing is fed
	uint8_t  ack[12];                // the Response header to send (ackReady)

	// the PDU being received
	uint32_t src;
	uint32_t dst;
	uint8_t  haveHeader;
	uint8_t  enc;                    // an ENC header came with it: not read, not acknowledged
	uint8_t  confirmed;
	uint8_t  group;
	uint8_t  defined;
	uint8_t  ddFormat;
	uint8_t  padBytes;
	uint8_t  respReq;                // header byte 0 bit 6: response requested
	uint8_t  hdrSeq;                 // header byte 9: N(S) of a confirmed PDU
	uint8_t  blkLen;                 // payload bytes per block
	uint8_t  count;                  // blocks in acc[]

	// the kept copy of the last PDU that ended incomplete (merged with its retransmission)
	uint32_t altSrc;
	uint32_t altDst;
	uint8_t  altCount;
	uint8_t  altBlkLen;
	uint8_t  altHdr;
} dmrSmsRx_t;

void dmrSmsRxInit(dmrSmsRx_t *rx);
// One CRC valid data burst: 12 payload bytes for types 6 and 7, 18 for type 8. Returns true when
// it completed a PDU (rx->ready is then set; ackReady too when it must be acknowledged).
bool dmrSmsRxFeed(dmrSmsRx_t *rx, int dataType, const uint8_t *payload, uint32_t ourId);
// Terminator: keep an incomplete PDU for a merge with its retransmission, drop the accumulator
void dmrSmsRxCallEnd(dmrSmsRx_t *rx);

// Is a completed PDU for us: a group message (any talkgroup) or private to our ID / all call
bool dmrSmsPduForUs(const dmrSmsPduInfo_t *info, uint32_t ourId);

typedef struct
{
	uint8_t  format;     // DMR_SMS_FMT_*
	uint16_t ipId;       // IPv4 identification (IP format), for the duplicate filter
	uint8_t  seq;        // TMS / DMR format message sequence
} dmrSmsPduMeta_t;

// Text of a completed PDU (CRC32 checked here). 'text' gets at most textMax - 1 characters and a
// NUL. Returns the length, 0 or less when it is not a readable message.
int dmrSmsPduDecode(const uint8_t *pdu, const dmrSmsPduInfo_t *info, char *text, int textMax, dmrSmsPduMeta_t *meta);

// Fork: DMR SMS send. TX path ported from dondch/OpenGD77-AES256 (cleartext only): the stock
// TYT cleartext "IP" SMS as their dmr_sms.c builds it, IPv4 (12.x.x.x source, 12.x.x.x private /
// 225.x.x.x group destination, the 24 bit IDs) / UDP 4007 / TMS (A0 00 seq 04, CR LF, UTF-16LE
// text), carried as an Unconfirmed PDU (no response requested, SAP 4) in rate 1/2 blocks after
// a CSBK preamble, for a private and a group message alike.
#define DMR_SMS_TX_PREAMBLE    6U                      // CSBK preamble bursts before the data header
#define DMR_SMS_TX_BLOCK_LEN   12U                     // rate 1/2, unconfirmed
#define DMR_SMS_TX_BURSTS_MAX  (DMR_SMS_TX_PREAMBLE + 1U + (DMR_SMS_PDU_MAX / DMR_SMS_TX_BLOCK_LEN))

// Builds the blocks in pdu[] (12 bytes each: the packet, zero pad, the CRC32) and the data header
// burst in hdr[] (CRC included). 'text': printable ASCII, cut at DMR_SMS_TEXT_MAX characters.
// Returns the number of blocks, 0 when it does not fit pduMax or the text is empty.
int dmrSmsTxBuild(uint8_t *pdu, int pduMax, uint8_t hdr[12], const char *text, uint32_t src, uint32_t dst, bool group,
		uint16_t ipId, uint8_t tmsSeq);
// One preamble CSBK burst (CRC included): 'following' = bursts after it (preambles, header, blocks)
void dmrSmsTxPreamble(uint8_t out[12], uint32_t src, uint32_t dst, bool group, uint8_t following);

// Lends pdu[] to a transmission: false (nothing lent) while a PDU is being received or one waits
// for the main loop. Until dmrSmsRxRelease() the feed ignores every burst; the kept copy of an
// incomplete PDU is given up.
bool dmrSmsRxHold(dmrSmsRx_t *rx);
void dmrSmsRxRelease(dmrSmsRx_t *rx);

// Helpers, also used by the host test to build vectors
uint32_t dmrSmsCrc32(const uint8_t *pdu, int len);      // over len - 4 bytes, wire order
void dmrSmsHdrCrc(const uint8_t *hdr10, uint8_t out2[2]); // data header CRC-CCITT, mask 0xCCCC
char dmrSmsMapChar(uint32_t codepoint);                  // 0: drop, else a printable ASCII character

#endif
