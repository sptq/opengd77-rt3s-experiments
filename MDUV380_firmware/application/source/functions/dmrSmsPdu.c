/*
 * Fork: DMR SMS receive (data PDU reassembly, text decoding) and send (the cleartext PDU builder,
 * see functions/dmrSmsPdu.h), by Marcin SP7TQ. RX and TX paths ported from dondch/OpenGD77-AES256 (cleartext only): the block
 * accumulation, the CRC32 completion rule, the retransmission merge, the Response header and
 * the three text formats follow their functions/dmr_sms.c; no AES, key, provisioning or
 * diagnostic code was taken over.
 *
 * No hardware access here: the interrupt side (dmrSmsRxFeed) runs from the HR-C6000 ISR, the
 * decoder from the main loop (functions/dmrSms.c), both are exercised by the host test
 * tests/aprs_rx/test_dmr_sms.c.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include "functions/dmrSmsPdu.h"

#if defined(HAS_DMR_SMS)

// Data header DPF (byte 0 bits 3..0)
#define DPF_RESPONSE       0x01U
#define DPF_UNCONFIRMED    0x02U
#define DPF_CONFIRMED      0x03U
#define DPF_DEFINED_SHORT  0x0DU
#define DPF_RAW_SHORT      0x0EU
#define DPF_PROPRIETARY    0x0FU

#define BLK_R12_LEN        12U
#define BLK_R34_LEN        18U
#define BLK_CONFIRMED_OFF  2U
#define ALT_MAX_DIFF_BLOCKS 4      // at most 2^4 - 2 = 14 CRC32 passes in the ISR

#define UDP_TMS_PORT       4007U
#define TYT_FMT_PREFIX     5

// --- CRCs ---------------------------------------------------------------------------------
static uint16_t crc16Ccitt(const uint8_t *data, int len) // poly 0x1021, ^0xFFFF
{
	uint16_t crc = 0;

	for (int i = 0; i < len; i++)
	{
		for (int k = 7; k >= 0; k--)
		{
			int bit = ((data[i] >> k) & 1);

			if ((((crc >> 15) & 1) ^ bit) != 0)
			{
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			}
			else
			{
				crc = (uint16_t)(crc << 1);
			}
		}
	}
	return (uint16_t)(crc ^ 0xFFFFU);
}

void dmrSmsHdrCrc(const uint8_t *hdr10, uint8_t out2[2])
{
	uint16_t v = (uint16_t)(crc16Ccitt(hdr10, 10) ^ 0xCCCCU);

	out2[0] = (uint8_t)(v >> 8);
	out2[1] = (uint8_t)(v & 0xFFU);
}

// Byte i of a candidate PDU: from acc[] when its block's bit is set in newMask, else pdu[]
static inline uint8_t candByte(const dmrSmsRx_t *rx, int i, uint32_t newMask, int blkLen)
{
	int b = (i / blkLen);

	return (((newMask >> b) & 1U) ? rx->acc[i] : rx->pdu[i]);
}

// DMR data PDU CRC32: byte pairs swapped, poly 0x04C11DB7, over the first len * 8 - 32 bits,
// result in wire order (compare with the last 4 bytes read big endian)
static uint32_t crc32Over(const uint8_t *a, const uint8_t *b, int blkLen, uint32_t newMask, int len)
{
	uint32_t crc = 0;
	int nbits = ((len * 8) - 32);
	int bitno = 0;

	for (int i = 0; (i + 1) < len; i += 2)
	{
		for (int pass = 0; pass < 2; pass++)
		{
			int idx = ((pass == 0) ? (i + 1) : i);
			uint8_t byte;

			if (b == NULL)
			{
				byte = a[idx];
			}
			else
			{
				int blk = (idx / blkLen);
				byte = (((newMask >> blk) & 1U) ? a[idx] : b[idx]);
			}

			for (int k = 7; k >= 0; k--)
			{
				if (bitno >= nbits)
				{
					goto done;
				}

				int bit = ((byte >> k) & 1);

				if ((((crc >> 31) & 1U) ^ (uint32_t)bit) != 0U)
				{
					crc = ((crc << 1) ^ 0x04C11DB7U);
				}
				else
				{
					crc = (crc << 1);
				}
				bitno++;
			}
		}
	}
done:
	return (((crc & 0xFFU) << 24) | ((crc & 0xFF00U) << 8) | ((crc >> 8) & 0xFF00U) | ((crc >> 24) & 0xFFU));
}

uint32_t dmrSmsCrc32(const uint8_t *pdu, int len)
{
	return crc32Over(pdu, NULL, 1, 0U, len);
}

static bool candCrcOk(const dmrSmsRx_t *rx, int total, uint32_t newMask, int blkLen)
{
	if (total < 8)
	{
		return false;
	}

	uint32_t want = (((uint32_t)candByte(rx, (total - 4), newMask, blkLen) << 24) | ((uint32_t)candByte(rx, (total - 3), newMask, blkLen) << 16) |
			((uint32_t)candByte(rx, (total - 2), newMask, blkLen) << 8) | (uint32_t)candByte(rx, (total - 1), newMask, blkLen));

	if (newMask == 0xFFFFFFFFU)
	{
		return (crc32Over(rx->acc, NULL, 1, 0U, total) == want);
	}
	return (crc32Over(rx->acc, rx->pdu, blkLen, newMask, total) == want);
}

// --- reassembly (interrupt context) ------------------------------------------------------
static void accReset(dmrSmsRx_t *rx)
{
	rx->haveHeader = 0U;
	rx->enc = 0U;
	rx->confirmed = 0U;
	rx->defined = 0U;
	rx->count = 0U;
	rx->blkLen = BLK_R12_LEN;
	rx->padBytes = 0U;
	rx->respReq = 0U;
}

void dmrSmsRxInit(dmrSmsRx_t *rx)
{
	memset(rx, 0, sizeof(dmrSmsRx_t));
	accReset(rx);
}

static uint8_t identity(const dmrSmsRx_t *rx)
{
	return (uint8_t)((rx->confirmed ? DPF_CONFIRMED : DPF_UNCONFIRMED) | (rx->defined ? 0x40U : 0U) | (rx->group ? 0x80U : 0U));
}

void dmrSmsRxCallEnd(dmrSmsRx_t *rx)
{
	int total = (rx->count * rx->blkLen);

	if (rx->hold)
	{
		return;
	}

	if (rx->haveHeader && (rx->enc == 0U) && (rx->count >= 2U) && (rx->ready == 0U) && (total <= (int)DMR_SMS_PDU_MAX))
	{
		memcpy(rx->pdu, rx->acc, (size_t)total);
		rx->altCount = rx->count;
		rx->altBlkLen = rx->blkLen;
		rx->altHdr = identity(rx);
		rx->altSrc = rx->src;
		rx->altDst = rx->dst;
	}
	accReset(rx);
}

static void dataHeader(dmrSmsRx_t *rx, const uint8_t *p)
{
	uint8_t dpf = (uint8_t)(p[0] & 0x0FU);

	if ((dpf == DPF_PROPRIETARY) && (p[1] == 0x10U))
	{
		// Motorola ENC extended header (MFID 0x10): the blocks that follow are encrypted. They are
		// not read and not acknowledged; the PDU is dropped when it completes.
		rx->enc = 1U;
		rx->count = 0U;
		return;
	}

	if ((dpf != DPF_UNCONFIRMED) && (dpf != DPF_CONFIRMED) && (dpf != DPF_DEFINED_SHORT) && (dpf != DPF_RAW_SHORT))
	{
		return; // a Response (somebody's acknowledgement), UDT, other proprietary headers: not ours to reassemble
	}

	rx->group = ((p[0] & 0x80U) ? 1U : 0U);
	rx->respReq = ((p[0] & 0x40U) ? 1U : 0U);
	rx->dst = (((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4]);
	rx->src = (((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7]);
	rx->confirmed = ((dpf == DPF_CONFIRMED) ? 1U : 0U);
	rx->defined = (((dpf == DPF_DEFINED_SHORT) || (dpf == DPF_RAW_SHORT)) ? 1U : 0U);
	rx->ddFormat = ((dpf == DPF_DEFINED_SHORT) ? (uint8_t)(p[8] >> 2) : 0x03U); // raw: 8 bit text assumed
	rx->hdrSeq = p[9];
	// pad octets: short data gives bits in byte 9, the others an octet count in byte 0 bits 5..4 + byte 1 bits 3..0
	rx->padBytes = (rx->defined ? (uint8_t)(p[9] / 8U) : (uint8_t)((((p[0] >> 4) & 0x03U) << 4) | (p[1] & 0x0FU)));
	rx->blkLen = BLK_R12_LEN;
	rx->count = 0U;
	rx->enc = 0U;
	rx->haveHeader = 1U;

	// a different packet: the kept copy cannot be a retransmission of this one
	if (rx->altCount && ((rx->src != rx->altSrc) || (rx->dst != rx->altDst) || (identity(rx) != rx->altHdr)))
	{
		rx->altCount = 0U;
	}
}

static void buildAck(dmrSmsRx_t *rx, uint32_t ourId)
{
	// ETSI Response packet: DPF 1, SAP 4, no blocks, class 0 / type ACK / status = N(S) of the
	// confirmed packet (short data has no N(S)). Sent by a stock receiver in the next timeslot.
	uint8_t ns = (rx->confirmed ? (uint8_t)((rx->hdrSeq >> 4) & 0x07U) : 0U);

	rx->ack[0] = 0x01U;
	rx->ack[1] = 0x40U;
	rx->ack[2] = (uint8_t)(rx->src >> 16);
	rx->ack[3] = (uint8_t)(rx->src >> 8);
	rx->ack[4] = (uint8_t)rx->src;
	rx->ack[5] = (uint8_t)(ourId >> 16);
	rx->ack[6] = (uint8_t)(ourId >> 8);
	rx->ack[7] = (uint8_t)ourId;
	rx->ack[8] = 0x00U;
	rx->ack[9] = (uint8_t)(0x08U | ns);
	dmrSmsHdrCrc(rx->ack, &rx->ack[10]);
	rx->ackReady = 1U;
}

bool dmrSmsRxFeed(dmrSmsRx_t *rx, int dataType, const uint8_t *p, uint32_t ourId)
{
	if (rx->hold)
	{
		return false; // pdu[] is being transmitted
	}

	if (dataType == DMR_SMS_DT_DATA_HEADER)
	{
		dataHeader(rx, p);
		return false;
	}

	if (((dataType != DMR_SMS_DT_RATE12_DATA) && (dataType != DMR_SMS_DT_RATE34_DATA)) || (rx->haveHeader == 0U))
	{
		return false;
	}

	uint8_t raw = ((dataType == DMR_SMS_DT_RATE34_DATA) ? BLK_R34_LEN : BLK_R12_LEN);
	uint8_t off = (rx->confirmed ? BLK_CONFIRMED_OFF : 0U);
	uint8_t blkLen = (uint8_t)(raw - off);

	if ((rx->count > 0U) && (blkLen != rx->blkLen))
	{
		rx->count = 0U; // one rate per PDU: a block of another rate starts over
	}
	if ((((rx->count + 1U) * blkLen) > DMR_SMS_PDU_MAX) || (rx->ready != 0U))
	{
		return false; // too long for us, or the last PDU is still with the main loop
	}

	rx->blkLen = blkLen;
	memcpy(&rx->acc[rx->count * blkLen], (p + off), blkLen);
	rx->count++;

	// Complete when the blocks so far end with a valid packet CRC32 (checked from the first
	// block: a stock private SMS in the DMR format is a single rate 3/4 block)
	int total = (rx->count * blkLen);
	uint32_t mask = 0xFFFFFFFFU; // blocks of the accepted PDU taken from this copy
	bool stripped = false;       // pdu[] already holds the PDU (confirmed layout of short data)

	// Short data with a response requested: BrandMeister sends it in confirmed blocks (DBSN +
	// CRC-9 in the first 2 bytes of each rate 3/4 block, seen on the air from 262994), other
	// senders in unconfirmed ones. The header does not say, so the packet CRC32 decides. pdu[]
	// is free for the check while no copy of a failed packet is kept in it.
	if (rx->defined && rx->respReq && (rx->altCount == 0U) && (rx->ready == 0U) && (candCrcOk(rx, total, mask, blkLen) == false))
	{
		int sl = (blkLen - BLK_CONFIRMED_OFF);
		int t2 = (rx->count * sl);

		if (t2 >= 8)
		{
			for (int b = 0; b < rx->count; b++)
			{
				memcpy(&rx->pdu[b * sl], &rx->acc[(b * blkLen) + BLK_CONFIRMED_OFF], (size_t)sl);
			}
			uint32_t want = (((uint32_t)rx->pdu[t2 - 4] << 24) | ((uint32_t)rx->pdu[t2 - 3] << 16) | ((uint32_t)rx->pdu[t2 - 2] << 8) | rx->pdu[t2 - 1]);

			if (dmrSmsCrc32(rx->pdu, t2) == want)
			{
				stripped = true;
				total = t2;
			}
		}
	}

	if ((stripped == false) && (candCrcOk(rx, total, mask, blkLen) == false))
	{
		// The chip's CRC flag on a rate 3/4 block does not cover its CRC-9, so a damaged block can
		// get here. If the last copy of this packet was kept and differs in a few blocks, one of
		// the combinations of the two may be the packet; the CRC32 decides.
		uint32_t diff = 0U;
		int ndiff = 0;

		if ((rx->altCount != rx->count) || (rx->altBlkLen != blkLen) || (rx->enc != 0U) ||
				(rx->altSrc != rx->src) || (rx->altDst != rx->dst) || (rx->altHdr != identity(rx)))
		{
			return false; // keep accumulating
		}

		for (int b = 0; b < rx->count; b++)
		{
			if (memcmp(&rx->acc[b * blkLen], &rx->pdu[b * blkLen], blkLen) != 0)
			{
				diff |= (1U << b);
				ndiff++;
			}
		}
		if ((ndiff == 0) || (ndiff > ALT_MAX_DIFF_BLOCKS))
		{
			return false;
		}

		mask = 0U;
		for (uint32_t sub = ((diff - 1U) & diff); sub != 0U; sub = ((sub - 1U) & diff))
		{
			if (candCrcOk(rx, total, sub, blkLen))
			{
				mask = sub;
				break;
			}
		}
		if (mask == 0U)
		{
			return false;
		}
	}

	if (rx->enc)
	{
		accReset(rx); // encrypted: dropped, not acknowledged
		rx->altCount = 0U;
		return false;
	}

	if (stripped == false)
	{
		for (int i = 0; i < total; i++)
		{
			rx->pdu[i] = candByte(rx, i, mask, blkLen);
		}
	}
	rx->altCount = 0U;
	rx->info.len = (uint16_t)total;
	rx->info.src = rx->src;
	rx->info.dst = rx->dst;
	rx->info.group = rx->group;
	rx->info.defined = rx->defined;
	rx->info.ddFormat = rx->ddFormat;
	rx->info.padBytes = rx->padBytes;

	// Confirmed (or short data), private, to us, response requested: acknowledge at the link
	// layer, whatever the main loop then makes of the text (a stock radio does the same)
	if ((rx->confirmed || rx->defined) && (rx->group == 0U) && rx->respReq && (rx->dst == ourId) && (ourId != 0U))
	{
		buildAck(rx, ourId);
	}

	rx->ready = 1U;
	accReset(rx);
	return true;
}

bool dmrSmsRxHold(dmrSmsRx_t *rx)
{
	// set first: from here on the interrupt leaves everything alone, then look at what it left
	rx->hold = 1U;
	if (rx->ready || (rx->haveHeader && (rx->count > 0U)))
	{
		rx->hold = 0U;
		return false;
	}
	rx->altCount = 0U;
	accReset(rx);
	return true;
}

void dmrSmsRxRelease(dmrSmsRx_t *rx)
{
	rx->altCount = 0U;
	accReset(rx);
	rx->hold = 0U;
}

bool dmrSmsPduForUs(const dmrSmsPduInfo_t *info, uint32_t ourId)
{
	return (info->group || (info->dst == ourId) || (info->dst == 0xFFFFFFU));
}

// --- text (main loop) --------------------------------------------------------------------
static const char latin1Base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static const char latinExtABase[] =
		"AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiJjJjKkkLlLlLlL"
		"lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";

char dmrSmsMapChar(uint32_t cp)
{
	if ((cp >= 0x20U) && (cp < 0x7FU))
	{
		return (char)cp;
	}
	if ((cp == 0x0AU) || (cp == 0x0DU) || (cp == 0x09U) || (cp == 0xA0U))
	{
		return ' ';
	}
	if (cp < 0xA0U)
	{
		return 0; // controls
	}
	if (cp >= 0xC0U)
	{
		if (cp <= 0xFFU)
		{
			return latin1Base[cp - 0xC0U];
		}
		if (cp <= 0x17FU)
		{
			return latinExtABase[cp - 0x100U];
		}
	}
	switch (cp)
	{
		case 0x2018U: case 0x2019U: case 0xB4U: return '\'';
		case 0x201CU: case 0x201DU: case 0x201EU: case 0xABU: case 0xBBU: return '"';
		case 0x2013U: case 0x2014U: return '-';
		case 0x2026U: return '.';
		case 0xB0U: return 'o';
		case 0x20ACU: return 'E';
		default: return '?';
	}
}

// ISO 8859-2 bytes that differ from Latin-1 for the Polish letters
static uint32_t latin2ToCodepoint(uint8_t b)
{
	static const uint8_t from[16] = { 0xA1, 0xA3, 0xA6, 0xAC, 0xAF, 0xB1, 0xB3, 0xB6, 0xBC, 0xBF, 0xC6, 0xCA, 0xD1, 0xE6, 0xEA, 0xF1 };
	static const uint16_t to[16] = { 0x104, 0x141, 0x15A, 0x179, 0x17B, 0x105, 0x142, 0x15B, 0x17A, 0x17C, 0x106, 0x118, 0x143, 0x107, 0x119, 0x144 };

	for (int i = 0; i < 16; i++)
	{
		if (from[i] == b)
		{
			return to[i];
		}
	}
	return b;
}

// Text in a DD format (ETSI TS 102 361-1 table: 0x02 7 bit, 0x03 8859-1, 0x04 8859-2, 0x05..0x11
// other 8 bit sets, 0x12 UTF-8, 0x13 UTF-16, 0x14 UTF-16BE, 0x15 UTF-16LE, 0x16..0x18 UTF-32).
// Returns the length written, -1 when there is nothing readable in it.
static int textDecode(const uint8_t *p, int len, uint8_t fmt, char *out, int outMax)
{
	int n = 0;
	int printable = 0;
	int step;
	bool le = ((fmt == 0x15U) || (fmt == 0x18U));

	if ((len <= 0) || (outMax <= 1) || (fmt <= 0x01U) || (fmt == 0x08U) || (fmt == 0x09U) || (fmt > 0x18U))
	{
		return -1; // binary, BCD, manufacturer specific
	}
	step = ((fmt >= 0x16U) ? 4 : (((fmt >= 0x13U) && (fmt <= 0x15U)) ? 2 : 1));

	for (int i = 0; ((i + step - 1) < len) && (n < (outMax - 1)); )
	{
		uint32_t cp;

		if (step == 1)
		{
			cp = p[i++];
			if (fmt == 0x12U) // UTF-8
			{
				int more = ((cp >= 0xF0U) ? 3 : ((cp >= 0xE0U) ? 2 : ((cp >= 0xC0U) ? 1 : 0)));

				if (more)
				{
					cp &= (0x3FU >> more);
					while (more-- && (i < len) && ((p[i] & 0xC0U) == 0x80U))
					{
						cp = ((cp << 6) | (p[i++] & 0x3FU));
					}
				}
			}
			else if (fmt == 0x04U)
			{
				cp = latin2ToCodepoint((uint8_t)cp);
			}
			else if ((fmt != 0x03U) && (cp >= 0x80U))
			{
				cp = '?'; // other 8 bit sets: only ASCII is certain
			}
		}
		else if (step == 2)
		{
			cp = (le ? (uint32_t)(p[i] | (p[i + 1] << 8)) : (uint32_t)((p[i] << 8) | p[i + 1]));
			i += 2;
		}
		else
		{
			cp = (le ? ((uint32_t)p[i] | ((uint32_t)p[i + 1] << 8) | ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24))
					: (((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) | ((uint32_t)p[i + 2] << 8) | (uint32_t)p[i + 3]));
			i += 4;
		}

		if (cp == 0U)
		{
			break;
		}

		char c = dmrSmsMapChar(cp);

		if (c == 0)
		{
			continue;
		}
		if ((c == ' ') && ((n == 0) || (out[n - 1] == ' ')))
		{
			continue; // no leading or doubled spaces (CR LF)
		}
		out[n++] = c;
		if (c != ' ')
		{
			printable++;
		}
	}

	while ((n > 0) && (out[n - 1] == ' '))
	{
		n--;
	}
	out[n] = 0;
	return ((printable > 0) ? n : -1);
}

// IPv4 / UDP (port 4007) / TMS: [len 2][A0 00 seq 04] then the UTF-16LE text (the stock TYT
// starts it with CR LF); the IP total length bounds it
static int ipDecode(const uint8_t *p, int len, char *out, int outMax, dmrSmsPduMeta_t *meta)
{
	if ((len < 34) || ((p[0] >> 4) != 4U) || (p[9] != 0x11U))
	{
		return -1;
	}

	int ihl = ((p[0] & 0x0FU) * 4);
	int iptot = ((p[2] << 8) | p[3]);
	int udp = ihl;
	int start = (udp + 8 + 6);

	if (ihl < 20)
	{
		return -1;
	}
	if (iptot > len)
	{
		iptot = len;
	}
	if ((start > iptot) || ((((p[udp + 2] << 8) | p[udp + 3])) != UDP_TMS_PORT))
	{
		return -1;
	}

	meta->format = DMR_SMS_FMT_IP;
	meta->ipId = (uint16_t)((p[4] << 8) | p[5]);
	meta->seq = p[udp + 8 + 4];
	return textDecode(&p[start], (iptot - start), 0x15U, out, outMax);
}

int dmrSmsPduDecode(const uint8_t *pdu, const dmrSmsPduInfo_t *info, char *text, int textMax, dmrSmsPduMeta_t *meta)
{
	int len = info->len;
	int r;

	memset(meta, 0, sizeof(dmrSmsPduMeta_t));
	if ((len < 8) || (len > (int)DMR_SMS_PDU_MAX) || (textMax < 2))
	{
		return -1;
	}

	uint32_t want = (((uint32_t)pdu[len - 4] << 24) | ((uint32_t)pdu[len - 3] << 16) | ((uint32_t)pdu[len - 2] << 8) | (uint32_t)pdu[len - 1]);
	if (dmrSmsCrc32(pdu, len) != want)
	{
		return -1;
	}

	int body = (len - 4 - info->padBytes); // bytes before the pad octets and the CRC
	if (body <= 0)
	{
		return -1;
	}

	if (info->defined)
	{
		meta->format = DMR_SMS_FMT_SHORT;
		return textDecode(pdu, body, info->ddFormat, text, textMax);
	}

	r = ipDecode(pdu, (len - 4), text, textMax, meta);
	if (r > 0)
	{
		return r;
	}

	// the stock DMR format: [seq] 00 [00 | 02] 01 01, UTF-16BE text
	if ((body >= (TYT_FMT_PREFIX + 2)) && (pdu[1] == 0x00U) && (pdu[3] == 0x01U) && (pdu[4] == 0x01U))
	{
		memset(meta, 0, sizeof(dmrSmsPduMeta_t));
		meta->format = DMR_SMS_FMT_TYT;
		meta->seq = pdu[0];
		return textDecode(&pdu[TYT_FMT_PREFIX], (body - TYT_FMT_PREFIX), 0x14U, text, textMax);
	}
	return -1;
}

// --- send (main loop) --------------------------------------------------------------------
// Fork: TX path ported from dondch/OpenGD77-AES256 (dmr_sms.c build_plaintext / dmrSmsSend,
// cleartext only: no ENC header, no encryption).
#define TMS_HDR_LEN    10   // [len 2] A0 00 seq 04 0D 00 0A 00
#define CSBK_PREAMBLE  0xBDU // last block, opcode 0x3D (preamble)

// One's complement sum of big endian 16 bit words (an odd byte is padded with 0)
static uint32_t onesSum(uint32_t sum, const uint8_t *b, int len)
{
	for (int i = 0; i < len; i += 2)
	{
		sum += (((uint32_t)b[i] << 8) | (((i + 1) < len) ? b[i + 1] : 0U));
	}
	return sum;
}

static uint16_t onesFold(uint32_t sum)
{
	while (sum >> 16)
	{
		sum = ((sum & 0xFFFFU) + (sum >> 16));
	}
	return (uint16_t)(~sum & 0xFFFFU);
}

static void put24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 16);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)v;
}

void dmrSmsTxPreamble(uint8_t out[12], uint32_t src, uint32_t dst, bool group, uint8_t following)
{
	uint16_t v;

	out[0] = CSBK_PREAMBLE;
	out[1] = 0x00U;                          // FID: standard
	out[2] = (group ? 0xC0U : 0x80U);        // data follows, group / individual
	out[3] = following;                      // blocks to follow
	put24(&out[4], dst);
	put24(&out[7], src);
	v = (uint16_t)(crc16Ccitt(out, 10) ^ 0xA5A5U); // CSBK CRC mask
	out[10] = (uint8_t)(v >> 8);
	out[11] = (uint8_t)v;
}

int dmrSmsTxBuild(uint8_t *pdu, int pduMax, uint8_t hdr[12], const char *text, uint32_t src, uint32_t dst, bool group,
		uint16_t ipId, uint8_t tmsSeq)
{
	int n = (int)strlen(text);

	if (n > (int)DMR_SMS_TEXT_MAX)
	{
		n = (int)DMR_SMS_TEXT_MAX;
	}

	int tmsLen = (TMS_HDR_LEN + (2 * n));
	int udpLen = (8 + tmsLen);
	int ipLen = (20 + udpLen);
	int total = ((((ipLen + 4) + (int)DMR_SMS_TX_BLOCK_LEN - 1) / (int)DMR_SMS_TX_BLOCK_LEN) * (int)DMR_SMS_TX_BLOCK_LEN);
	int pad = (total - 4 - ipLen);
	uint8_t *ip = pdu;
	uint8_t *udp = &pdu[20];
	uint8_t *tms = &pdu[28];

	if ((n == 0) || (total > pduMax))
	{
		return 0;
	}
	memset(pdu, 0, (size_t)total);

	// TMS: length of what follows the length field, then the stock header and the CR LF
	tms[0] = (uint8_t)((tmsLen - 2) >> 8);
	tms[1] = (uint8_t)(tmsLen - 2);
	tms[2] = 0xA0U;
	tms[3] = 0x00U;
	tms[4] = tmsSeq;
	tms[5] = 0x04U;
	tms[6] = 0x0DU;
	tms[8] = 0x0AU;
	for (int i = 0; i < n; i++)
	{
		uint8_t c = (uint8_t)text[i];

		tms[TMS_HDR_LEN + (2 * i)] = (((c >= 0x20U) && (c < 0x7FU)) ? c : (uint8_t)'?'); // UTF-16LE
	}

	// IPv4
	ip[0] = 0x45U;
	ip[2] = (uint8_t)(ipLen >> 8);
	ip[3] = (uint8_t)ipLen;
	ip[4] = (uint8_t)(ipId >> 8);
	ip[5] = (uint8_t)ipId;
	ip[8] = 0x40U;                           // TTL 64
	ip[9] = 0x11U;                           // UDP
	ip[12] = 0x0CU;                          // 12.<source ID>
	put24(&ip[13], src);
	ip[16] = (group ? 0xE1U : 0x0CU);        // 225.<talkgroup> / 12.<destination ID>
	put24(&ip[17], dst);
	uint16_t c = onesFold(onesSum(0U, ip, 20));
	ip[10] = (uint8_t)(c >> 8);
	ip[11] = (uint8_t)c;

	// UDP 4007 -> 4007, checksum over the pseudo header (addresses, protocol, length) and the datagram
	udp[0] = 0x0FU;
	udp[1] = 0xA7U;
	udp[2] = 0x0FU;
	udp[3] = 0xA7U;
	udp[4] = (uint8_t)(udpLen >> 8);
	udp[5] = (uint8_t)udpLen;
	c = onesFold(onesSum((0x0011U + (uint32_t)udpLen), &ip[12], 8) + (onesSum(0U, udp, udpLen)));
	if (c == 0U)
	{
		c = 0xFFFFU;
	}
	udp[6] = (uint8_t)(c >> 8);
	udp[7] = (uint8_t)c;

	// packet CRC32 after the pad octets
	uint32_t crc = dmrSmsCrc32(pdu, total);
	pdu[total - 4] = (uint8_t)(crc >> 24);
	pdu[total - 3] = (uint8_t)(crc >> 16);
	pdu[total - 2] = (uint8_t)(crc >> 8);
	pdu[total - 1] = (uint8_t)crc;

	// Unconfirmed data header: group bit, no response, pad octet count, SAP 4 (IP based), the
	// addresses, F (full message) + blocks to follow, FSN 0
	int blocks = (total / (int)DMR_SMS_TX_BLOCK_LEN);

	hdr[0] = (uint8_t)((group ? 0x80U : 0x00U) | (((uint32_t)pad & 0x10U) ? 0x10U : 0x00U) | DPF_UNCONFIRMED);
	hdr[1] = (uint8_t)(0x40U | ((uint32_t)pad & 0x0FU));
	put24(&hdr[2], dst);
	put24(&hdr[5], src);
	hdr[8] = (uint8_t)(0x80U | ((uint32_t)blocks & 0x7FU));
	hdr[9] = 0x00U;
	dmrSmsHdrCrc(hdr, &hdr[10]);
	return blocks;
}

#endif
