/*
 * Host test for the DMR SMS receive path (functions/dmrSmsPdu.c): data PDUs built from the
 * layouts in dondch/OpenGD77-AES256 (dmr_sms.c), including the three stock TYT "DMR format"
 * captures quoted there (their CRC32s check the CRC implementation against real air data),
 * fed burst by burst as the HR-C6000 hands them over. The send side (dmrSmsTxBuild) is checked
 * against an independent build of the same packet and round tripped through the receive path.
 */
#include <stdio.h>
#include <string.h>
#include "functions/dmrSmsPdu.h"

#define OUR_ID   2601234U
#define PEER_ID  2609876U
#define TG       260U

static int failures;
static dmrSmsRx_t rx;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void header(uint8_t h[12], uint8_t dpf, int group, int resp, uint32_t dst, uint32_t src, uint8_t blocks, uint8_t poc, uint8_t byte8, uint8_t byte9)
{
	h[0] = (uint8_t)((group ? 0x80 : 0) | (resp ? 0x40 : 0) | (((poc >> 4) & 3) << 4) | dpf);
	h[1] = (uint8_t)(0x40 | (poc & 0x0F)); // SAP 4
	h[2] = (uint8_t)(dst >> 16); h[3] = (uint8_t)(dst >> 8); h[4] = (uint8_t)dst;
	h[5] = (uint8_t)(src >> 16); h[6] = (uint8_t)(src >> 8); h[7] = (uint8_t)src;
	h[8] = (byte8 != 0xFF) ? byte8 : (uint8_t)(0x80 | blocks);
	h[9] = byte9;
	dmrSmsHdrCrc(h, &h[10]);
}

// payload -> PDU of whole blocks: zero pad, CRC32 at the end. Returns its length, *poc the pad count.
static int makePdu(uint8_t *pdu, const uint8_t *payload, int n, int blkLen, uint8_t *poc)
{
	int total = (((n + 4) + blkLen - 1) / blkLen) * blkLen;

	memset(pdu, 0, total);
	memcpy(pdu, payload, n);
	*poc = (uint8_t)(total - 4 - n);
	uint32_t c = dmrSmsCrc32(pdu, total);
	pdu[total - 4] = (uint8_t)(c >> 24); pdu[total - 3] = (uint8_t)(c >> 16);
	pdu[total - 2] = (uint8_t)(c >> 8);  pdu[total - 1] = (uint8_t)c;
	return total;
}

// Feeds the blocks of a PDU; corrupt >= 0 damages that block. Returns the number of completions.
static int feedBlocks(const uint8_t *pdu, int len, int confirmed, int rate34, int corrupt)
{
	int raw = (rate34 ? 18 : 12);
	int blkLen = raw - (confirmed ? 2 : 0);
	int done = 0;

	for (int b = 0; (b * blkLen) < len; b++)
	{
		uint8_t burst[18] = { 0 };

		if (confirmed)
		{
			burst[0] = (uint8_t)(b << 1); // DBSN, CRC-9 not checked
		}
		memcpy(&burst[confirmed ? 2 : 0], &pdu[b * blkLen], blkLen);
		if (b == corrupt)
		{
			burst[(confirmed ? 2 : 0) + 1] ^= 0x5A;
		}
		done += dmrSmsRxFeed(&rx, (rate34 ? DMR_SMS_DT_RATE34_DATA : DMR_SMS_DT_RATE12_DATA), burst, OUR_ID) ? 1 : 0;
	}
	return done;
}

static int decode(char *text, dmrSmsPduMeta_t *meta)
{
	int r = dmrSmsPduDecode(rx.pdu, &rx.info, text, (int)DMR_SMS_TEXT_MAX + 1, meta);
	rx.ready = 0;
	return r;
}

// IPv4 / UDP 4007 / TMS, UTF-16LE text from UTF-16 code units
static int buildIp(uint8_t *out, const uint16_t *units, int nUnits, uint32_t src, uint32_t dst, int group, uint8_t seq, uint16_t ipId)
{
	int tmsLen = 10 + (2 * nUnits);
	int udpLen = 8 + tmsLen;
	int tot = 20 + udpLen;
	uint8_t *ip = out, *udp = out + 20, *tms = udp + 8;

	memset(out, 0, tot);
	ip[0] = 0x45; ip[2] = (uint8_t)(tot >> 8); ip[3] = (uint8_t)tot;
	ip[4] = (uint8_t)(ipId >> 8); ip[5] = (uint8_t)ipId; ip[8] = 64; ip[9] = 0x11;
	ip[12] = 0x0C; ip[13] = (uint8_t)(src >> 16); ip[14] = (uint8_t)(src >> 8); ip[15] = (uint8_t)src;
	ip[16] = (group ? 0xE1 : 0x0C); ip[17] = (uint8_t)(dst >> 16); ip[18] = (uint8_t)(dst >> 8); ip[19] = (uint8_t)dst;
	udp[0] = 0x0F; udp[1] = 0xA7; udp[2] = 0x0F; udp[3] = 0xA7; udp[4] = (uint8_t)(udpLen >> 8); udp[5] = (uint8_t)udpLen;
	tms[0] = (uint8_t)((8 + 2 * nUnits) >> 8); tms[1] = (uint8_t)(8 + 2 * nUnits);
	tms[2] = 0xA0; tms[3] = 0x00; tms[4] = seq; tms[5] = 0x04;
	tms[6] = 0x0D; tms[7] = 0x00; tms[8] = 0x0A; tms[9] = 0x00;
	for (int i = 0; i < nUnits; i++)
	{
		tms[10 + 2 * i] = (uint8_t)units[i];
		tms[11 + 2 * i] = (uint8_t)(units[i] >> 8);
	}
	return tot;
}

static int asciiUnits(uint16_t *u, const char *s)
{
	int n = 0;
	while (*s)
	{
		u[n++] = (uint8_t)*s++;
	}
	return n;
}

static void testTytCaptures(void)
{
	// Stock private SMS "gm" / "gt" in the DMR format: one Confirmed rate 3/4 block, POC 3
	static const uint8_t gm[16] = { 0x03, 0x00, 0x00, 0x01, 0x01, 0x00, 0x67, 0x00, 0x6D, 0x00, 0x00, 0x00, 0x95, 0xBF, 0xFE, 0x7A };
	static const uint8_t gt[16] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x00, 0x67, 0x00, 0x74, 0x00, 0x00, 0x00, 0xBE, 0x13, 0xAC, 0x8D };
	uint8_t h[12];
	char text[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;

	CHECK(dmrSmsCrc32(gm, 16) == 0x95BFFE7AU, "CRC32 of the stock 'gm' capture: %08X", dmrSmsCrc32(gm, 16));
	CHECK(dmrSmsCrc32(gt, 16) == 0xBE13AC8DU, "CRC32 of the stock 'gt' capture: %08X", dmrSmsCrc32(gt, 16));

	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID, 1, 3, 0xFF, 0x80 | (5 << 4));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(gm, 16, 1, 1, -1) == 1, "gm: one completion");
	CHECK(dmrSmsPduForUs(&rx.info, OUR_ID), "gm: for us");
	CHECK(rx.ackReady, "gm: acknowledgement built");
	{
		uint8_t want[12] = { 0x01, 0x40, (uint8_t)(PEER_ID >> 16), (uint8_t)(PEER_ID >> 8), (uint8_t)PEER_ID,
				(uint8_t)(OUR_ID >> 16), (uint8_t)(OUR_ID >> 8), (uint8_t)OUR_ID, 0x00, 0x08 | 5, 0, 0 };
		dmrSmsHdrCrc(want, &want[10]);
		CHECK(memcmp(rx.ack, want, 12) == 0, "gm: ACK header (DPF 1, SAP 4, ACK N(S) 5)");
	}
	CHECK(decode(text, &meta) == 2 && strcmp(text, "gm") == 0, "gm: text '%s'", text);
	CHECK(meta.format == DMR_SMS_FMT_TYT && meta.seq == 3, "gm: DMR format, seq 3");

	// same capture with the response bit clear: no acknowledgement
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 0, OUR_ID, PEER_ID, 1, 3, 0xFF, 0x80);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(gt, 16, 1, 1, -1);
	CHECK(rx.ready && (rx.ackReady == 0), "gt: no response requested, no ACK");
	CHECK(decode(text, &meta) == 2 && strcmp(text, "gt") == 0, "gt: text '%s'", text);

	// Stock group SMS "gtw": two Unconfirmed rate 1/2 blocks, POC 9, CRC 94 BA C0 6D (sequence byte unknown)
	int matches = 0, seqFound = -1;
	for (int seq = 0; seq < 256; seq++)
	{
		uint8_t gtw[24] = { (uint8_t)seq, 0x00, 0x02, 0x01, 0x01, 0x00, 0x67, 0x00, 0x74, 0x00, 0x77 };
		gtw[20] = 0x94; gtw[21] = 0xBA; gtw[22] = 0xC0; gtw[23] = 0x6D;
		if (dmrSmsCrc32(gtw, 24) == 0x94BAC06DU)
		{
			matches++;
			seqFound = seq;
		}
	}
	CHECK(matches == 1, "gtw: the stock group capture's CRC32 matches one sequence byte (%d matches)", matches);
	if (matches == 1)
	{
		uint8_t gtw[24] = { (uint8_t)seqFound, 0x00, 0x02, 0x01, 0x01, 0x00, 0x67, 0x00, 0x74, 0x00, 0x77 };
		gtw[20] = 0x94; gtw[21] = 0xBA; gtw[22] = 0xC0; gtw[23] = 0x6D;
		dmrSmsRxInit(&rx);
		header(h, 0x02, 1, 0, TG, PEER_ID, 2, 9, 0xFF, 0);
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
		CHECK(feedBlocks(gtw, 24, 0, 0, -1) == 1, "gtw: completes after 2 blocks");
		CHECK(rx.info.group && dmrSmsPduForUs(&rx.info, OUR_ID) && !rx.ackReady, "gtw: group, accepted, no ACK");
		CHECK(decode(text, &meta) == 3 && strcmp(text, "gtw") == 0, "gtw: text '%s'", text);
	}
}

static void testIp(void)
{
	// Polish pangram in UTF-16: "Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"
	static const uint16_t pl[] = { 'Z', 'a', 0x17C, 0xF3, 0x142, 0x107, ' ', 'g', 0x119, 0x15B, 'l', 0x105, ' ', 'j', 'a', 0x17A, 0x144, ' ',
			0x104, 0x106, 0x118, 0x141, 0x143, 0xD3, 0x15A, 0x179, 0x17B };
	uint8_t ip[400], pdu[400], h[12];
	uint8_t poc;
	char text[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;
	int n, len;

	// group, Unconfirmed rate 1/2
	n = buildIp(ip, pl, (int)(sizeof(pl) / 2), PEER_ID, TG, 1, 0x91, 0x000A);
	len = makePdu(pdu, ip, n, 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x02, 1, 0, TG, PEER_ID, (uint8_t)(len / 12), poc, 0xFF, 0);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 0, 0, -1) == 1, "IP group: one completion");
	CHECK(rx.info.src == PEER_ID && rx.info.dst == TG && rx.info.group, "IP group: addressing");
	CHECK(decode(text, &meta) > 0 && strcmp(text, "Zazolc gesla jazn ACELNOSZZ") == 0, "IP group: text '%s'", text);
	CHECK(meta.format == DMR_SMS_FMT_IP && meta.ipId == 0x000A && meta.seq == 0x91, "IP group: meta");

	// private, Confirmed rate 3/4, response requested: the stock private SMS
	uint16_t u[200];
	const char *msg = "Meeting point moved to the north gate, bring water and the spare battery pack. 73";
	n = buildIp(ip, u, asciiUnits(u, msg), PEER_ID, OUR_ID, 0, 0x92, 0x000B);
	len = makePdu(pdu, ip, n, 16, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID, (uint8_t)(len / 16), poc, 0xFF, 0x80 | (2 << 4));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 1, 1, -1) == 1, "IP private: one completion");
	CHECK(rx.ackReady && rx.ack[9] == (0x08 | 2) && rx.ack[0] == 0x01, "IP private: ACK with N(S) 2");
	CHECK(decode(text, &meta) > 0 && strcmp(text, msg) == 0, "IP private: text '%s'", text);

	// private to somebody else: completes, not for us, not acknowledged
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID + 1, PEER_ID, (uint8_t)(len / 16), poc, 0xFF, 0x80);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 1, 1, -1);
	CHECK(rx.ready && !dmrSmsPduForUs(&rx.info, OUR_ID) && !rx.ackReady, "IP private to another ID: ignored, no ACK");
	rx.ready = 0;

	// longest message kept: 140 characters (more are cut)
	char longMsg[161];
	for (int i = 0; i < 160; i++)
	{
		longMsg[i] = (char)('A' + (i % 26));
	}
	longMsg[160] = 0;
	n = buildIp(ip, u, asciiUnits(u, longMsg) - 16, PEER_ID, OUR_ID, 0, 1, 1); // 144 characters
	len = makePdu(pdu, ip, n, 16, &poc);
	CHECK(len <= (int)DMR_SMS_PDU_MAX, "144 characters fit the reassembly buffer (%d)", len);
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID, (uint8_t)(len / 16), poc, 0xFF, 0x80);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 1, 1, -1) == 1, "144 characters: completes");
	CHECK(decode(text, &meta) == (int)DMR_SMS_TEXT_MAX && strncmp(text, longMsg, DMR_SMS_TEXT_MAX) == 0, "144 characters: 140 kept");

	// GPS / LRRP on UDP 4001 is not a text message
	n = buildIp(ip, u, asciiUnits(u, "xyz"), PEER_ID, OUR_ID, 0, 1, 1);
	ip[22] = 0x0F; ip[23] = 0xA1;
	len = makePdu(pdu, ip, n, 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x02, 0, 0, OUR_ID, PEER_ID, (uint8_t)(len / 12), poc, 0xFF, 0);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 0, 0, -1);
	CHECK(rx.ready && decode(text, &meta) <= 0, "UDP port 4001: not decoded as text");
}

static void testRobustness(void)
{
	uint16_t u[64];
	uint8_t ip[200], pdu[200], h[12];
	uint8_t poc;
	char text[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;
	int n = buildIp(ip, u, asciiUnits(u, "Retransmission repair test"), PEER_ID, OUR_ID, 0, 5, 0x0042);
	int len = makePdu(pdu, ip, n, 16, &poc);

	// no header: nothing
	dmrSmsRxInit(&rx);
	CHECK(feedBlocks(pdu, len, 1, 1, -1) == 0 && !rx.ready, "blocks without a header: ignored");

	// ENC header after the data header: encrypted, dropped, never acknowledged
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID, (uint8_t)(len / 16 + 1), poc, 0xFF, 0x80);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	{
		uint8_t enc[12] = { 0x0F, 0x10, 0x11, 0x01, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0 };
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, enc, OUR_ID);
	}
	CHECK(feedBlocks(pdu, len, 1, 1, -1) == 0 && !rx.ready && !rx.ackReady, "ENC header: dropped, no ACK");

	// a damaged block in each of two copies (different blocks): merged into the packet
	dmrSmsRxInit(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID, (uint8_t)(len / 16), poc, 0xFF, 0x80 | (1 << 4));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 1, 1, 1) == 0, "copy 1 with block 1 damaged: incomplete");
	dmrSmsRxCallEnd(&rx);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 1, 1, 2) == 1, "copy 2 with block 2 damaged: merged with copy 1");
	CHECK(rx.ackReady, "merged packet: acknowledged");
	CHECK(decode(text, &meta) > 0 && strcmp(text, "Retransmission repair test") == 0, "merged: text '%s'", text);

	// a damaged block and a different packet next: no false merge
	dmrSmsRxInit(&rx);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 1, 1, 1);
	dmrSmsRxCallEnd(&rx);
	header(h, 0x03, 0, 1, OUR_ID, PEER_ID + 7, (uint8_t)(len / 16), poc, 0xFF, 0x80);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 1, 1, 2) == 0 && !rx.ready, "other sender: the kept copy is not used");

	// a corrupt CRC32 never completes, and the decoder checks it again
	dmrSmsRxInit(&rx);
	header(h, 0x02, 1, 0, TG, PEER_ID, 3, poc, 0xFF, 0);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	pdu[len - 1] ^= 1;
	CHECK(feedBlocks(pdu, len, 0, 1, -1) == 0, "bad CRC32: incomplete");
	pdu[len - 1] ^= 1;
	{
		dmrSmsPduInfo_t info = { .len = (uint16_t)len, .padBytes = poc };
		uint8_t bad[200];
		memcpy(bad, pdu, len);
		bad[3] ^= 0x10;
		CHECK(dmrSmsPduDecode(bad, &info, text, sizeof(text), &meta) <= 0, "decoder: bad CRC32 rejected");
		CHECK(dmrSmsPduDecode(pdu, &info, text, sizeof(text), &meta) > 0, "decoder: good CRC32 accepted");
	}
}

static void testShortData(void)
{
	uint8_t pdu[100], h[12];
	uint8_t poc;
	char text[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;

	// Defined Short Data, UTF-16BE (DD format 0x14), as a network delivers it: "CQ ąę"
	static const uint8_t be[] = { 0, 'C', 0, 'Q', 0, ' ', 0x01, 0x05, 0x01, 0x19 };
	int len = makePdu(pdu, be, sizeof(be), 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x0D, 0, 1, OUR_ID, PEER_ID, 1, 0, (uint8_t)(0x14 << 2), (uint8_t)(poc * 8));
	h[0] &= 0xCF; h[1] = 0xA0; dmrSmsHdrCrc(h, &h[10]); // SAP 10, appended blocks 0..
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(feedBlocks(pdu, len, 0, 0, -1) == 1, "short data: completes");
	CHECK(rx.info.defined && rx.info.ddFormat == 0x14, "short data: DD format 0x14");
	CHECK(rx.ackReady && rx.ack[9] == 0x08, "short data, response requested: ACK status 0");
	CHECK(decode(text, &meta) > 0 && strcmp(text, "CQ ae") == 0 && meta.format == DMR_SMS_FMT_SHORT, "short data: text '%s'", text);

	// BrandMeister service reply (262994 'RPT', seen on the air): Defined Short Data, response
	// requested, UTF-16BE, in CONFIRMED rate 3/4 blocks (16 payload bytes after DBSN + CRC-9)
	{
		static const char msg[] = "Static: TG 260, TG 2601 Dynamic: TG 9 Timeslot 2 ok";
		uint8_t be16[2 * sizeof(msg)];
		uint8_t big[200];
		int n = 0;

		for (const char *c = msg; *c; c++)
		{
			be16[n++] = 0; be16[n++] = (uint8_t)*c;
		}
		int blen = makePdu(big, be16, n, 16, &poc);
		dmrSmsRxInit(&rx);
		header(h, 0x0D, 0, 1, OUR_ID, 262994, 1, 0, (uint8_t)(0x14 << 2), (uint8_t)(poc * 8));
		h[0] &= 0xCF; h[1] = (uint8_t)(0xA0 | ((blen / 16) & 0x0F)); dmrSmsHdrCrc(h, &h[10]);
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
		CHECK(feedBlocks(big, blen, 1, 1, -1) == 1, "BM short data, confirmed rate 3/4: completes");
		CHECK(rx.ackReady, "BM short data: ACK prepared");
		CHECK(decode(text, &meta) > 0 && strcmp(text, msg) == 0, "BM short data: text '%s'", text);
	}

	// Raw Short Data: 8 bit text
	static const uint8_t raw[] = "Hello net";
	len = makePdu(pdu, raw, 9, 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x0E, 1, 0, TG, PEER_ID, 1, 0, 0x00, (uint8_t)(poc * 8));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 0, 0, -1);
	CHECK(rx.ready && decode(text, &meta) > 0 && strcmp(text, "Hello net") == 0, "raw short data: text '%s'", text);

	// Defined short data in ISO 8859-2 and UTF-8
	static const uint8_t l2[] = { 0xA3, 0xF3, 0x64, 0xBC };   // "Łódź"
	len = makePdu(pdu, l2, sizeof(l2), 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x0D, 1, 0, TG, PEER_ID, 1, 0, (uint8_t)(0x04 << 2), (uint8_t)(poc * 8));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 0, 0, -1);
	CHECK(rx.ready && decode(text, &meta) > 0 && strcmp(text, "Lodz") == 0, "8859-2: text '%s'", text);

	static const uint8_t u8[] = { 0xC5, 0x81, 0xC3, 0xB3, 'd', 0xC5, 0xBA, '!' };   // "Łódź!"
	len = makePdu(pdu, u8, sizeof(u8), 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x0D, 1, 0, TG, PEER_ID, 1, 0, (uint8_t)(0x12 << 2), (uint8_t)(poc * 8));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 0, 0, -1);
	CHECK(rx.ready && decode(text, &meta) > 0 && strcmp(text, "Lodz!") == 0, "UTF-8: text '%s'", text);

	// binary DD format: not a text
	static const uint8_t bin[] = { 1, 2, 3, 4 };
	len = makePdu(pdu, bin, sizeof(bin), 12, &poc);
	dmrSmsRxInit(&rx);
	header(h, 0x0D, 1, 0, TG, PEER_ID, 1, 0, 0x00, (uint8_t)(poc * 8));
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	feedBlocks(pdu, len, 0, 0, -1);
	CHECK(rx.ready && decode(text, &meta) <= 0, "binary short data: no text");

	// a Response header (somebody's ACK) in the middle of nothing does not start a PDU
	dmrSmsRxInit(&rx);
	header(h, 0x01, 0, 0, OUR_ID, PEER_ID, 0, 0, 0x00, 0x08);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, h, OUR_ID);
	CHECK(rx.haveHeader == 0, "Response header: ignored");
}

static void testMapChar(void)
{
	CHECK(dmrSmsMapChar('a') == 'a' && dmrSmsMapChar(0x142) == 'l' && dmrSmsMapChar(0x141) == 'L', "map: l / L");
	CHECK(dmrSmsMapChar(0xDF) == 's' && dmrSmsMapChar(0xE9) == 'e' && dmrSmsMapChar(0x2019) == '\'', "map: Latin-1, quotes");
	CHECK(dmrSmsMapChar(0x4E2D) == '?' && dmrSmsMapChar(0x07) == 0 && dmrSmsMapChar(0x0A) == ' ', "map: others");
}

static uint16_t onesFoldRef(const uint8_t *b, int len, uint32_t sum)
{
	for (int i = 0; i < len; i += 2)
	{
		sum += ((uint32_t)b[i] << 8) | ((i + 1 < len) ? b[i + 1] : 0);
	}
	while (sum >> 16)
	{
		sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return (uint16_t)sum;
}

// Builds with dmrSmsTxBuild, checks the framing, feeds header + blocks to the receiver
static void txRoundTrip(const char *text, uint32_t dst, int group, uint16_t ipId, uint8_t seq)
{
	uint8_t pdu[DMR_SMS_PDU_MAX];
	uint8_t hdr[12];
	uint8_t ref[DMR_SMS_PDU_MAX];
	uint16_t units[DMR_SMS_TEXT_MAX];
	char got[DMR_SMS_TEXT_MAX + 1];
	dmrSmsPduMeta_t meta;
	int n = (int)strlen(text);
	int blocks = dmrSmsTxBuild(pdu, sizeof(pdu), hdr, text, PEER_ID, dst, group, ipId, seq);
	int total = blocks * 12;
	int ipLen = 20 + 8 + 10 + 2 * n;

	CHECK(blocks == ((ipLen + 4 + 11) / 12), "tx '%.10s': %d blocks", text, blocks);
	if (blocks <= 0)
	{
		return;
	}

	// the packet, byte for byte, apart from the two checksums the reference leaves at 0
	int refLen = buildIp(ref, units, asciiUnits(units, text), PEER_ID, dst, group, seq, ipId);
	CHECK(refLen == ipLen, "tx: IP length %d", refLen);
	uint8_t a[DMR_SMS_PDU_MAX];
	memcpy(a, pdu, ipLen);
	a[10] = a[11] = a[26] = a[27] = 0;
	CHECK(memcmp(a, ref, ipLen) == 0, "tx '%.10s': IPv4 / UDP / TMS bytes", text);
	CHECK(onesFoldRef(pdu, 20, 0) == 0xFFFF, "tx: IPv4 header checksum");
	{
		uint8_t pseudo[12] = { 0 };
		memcpy(pseudo, &pdu[12], 8);
		pseudo[9] = 0x11; pseudo[10] = (uint8_t)((ipLen - 20) >> 8); pseudo[11] = (uint8_t)(ipLen - 20);
		CHECK(onesFoldRef(&pdu[20], ipLen - 20, onesFoldRef(pseudo, 12, 0)) == 0xFFFF, "tx: UDP checksum");
	}
	for (int i = ipLen; i < total - 4; i++)
	{
		CHECK(pdu[i] == 0, "tx: pad octet %d", i);
	}
	uint32_t crc = ((uint32_t)pdu[total - 4] << 24) | ((uint32_t)pdu[total - 3] << 16) | ((uint32_t)pdu[total - 2] << 8) | pdu[total - 1];
	CHECK(crc == dmrSmsCrc32(pdu, total), "tx: CRC32");

	// data header: Unconfirmed, no response, SAP 4, pad count, addresses, F + blocks, CRC-CCITT ^ CCCC
	{
		uint8_t want[12];
		header(want, 0x02, group, 0, dst, PEER_ID, (uint8_t)blocks, (uint8_t)(total - 4 - ipLen), 0xFF, 0x00);
		CHECK(memcmp(hdr, want, 12) == 0, "tx '%.10s': data header", text);
		uint8_t c2[2];
		dmrSmsHdrCrc(hdr, c2);
		CHECK(c2[0] == hdr[10] && c2[1] == hdr[11], "tx: header CRC");
	}

	// through the receiver
	dmrSmsRxInit(&rx);
	dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, hdr, OUR_ID);
	CHECK(feedBlocks(pdu, total, 0, 0, -1) == 1, "tx rx '%.10s': one completion", text);
	CHECK(rx.info.src == PEER_ID && rx.info.dst == dst && rx.info.group == (group ? 1 : 0), "tx rx: src %u dst %u group %u",
			rx.info.src, rx.info.dst, rx.info.group);
	CHECK(rx.ackReady == 0, "tx rx: unconfirmed, nothing to acknowledge");
	CHECK(decode(got, &meta) == n && strcmp(got, text) == 0, "tx rx: text '%s'", got);
	CHECK(meta.format == DMR_SMS_FMT_IP && meta.ipId == ipId && meta.seq == seq, "tx rx: IP format, id %04X seq %02X", meta.ipId, meta.seq);
}

static void testTx(void)
{
	char longText[DMR_SMS_TEXT_MAX + 11];
	uint8_t pdu[DMR_SMS_PDU_MAX];
	uint8_t hdr[12];

	txRoundTrip("WX", 262993U, 0, 0x1234, 0x91);
	txRoundTrip("INBOX", 262995U, 0, 0xFFFF, 0x9F);
	txRoundTrip("SP5ABC test 123", OUR_ID, 0, 0x0001, 0x90);
	txRoundTrip("CQ EMCOM net at 20Z", TG, 1, 0x8000, 0x92);
	txRoundTrip("x", 16777215U, 1, 0x0002, 0x93);   // 24 bit addresses
	for (int i = 0; i < (int)DMR_SMS_TEXT_MAX; i++)
	{
		longText[i] = (char)('A' + (i % 26));
	}
	longText[DMR_SMS_TEXT_MAX] = 0;
	txRoundTrip(longText, OUR_ID, 0, 0x4242, 0x94);  // the longest: 27 blocks

	// a longer text is cut at DMR_SMS_TEXT_MAX
	memset(longText, 'z', sizeof(longText) - 1);
	longText[sizeof(longText) - 1] = 0;
	CHECK(dmrSmsTxBuild(pdu, sizeof(pdu), hdr, longText, PEER_ID, OUR_ID, 0, 1, 0x90) == 27, "tx: cut at %u characters", DMR_SMS_TEXT_MAX);
	CHECK(dmrSmsTxBuild(pdu, sizeof(pdu), hdr, "", PEER_ID, OUR_ID, 0, 1, 0x90) == 0, "tx: empty text refused");
	CHECK(dmrSmsTxBuild(pdu, 36, hdr, "too long for 36", PEER_ID, OUR_ID, 0, 1, 0x90) == 0, "tx: buffer too small refused");

	// a known vector: "WX" to 262993 from 2609876, IP id 0x1234, seq 0x91 (values from this builder,
	// checked by hand against the layout of the original's build_plaintext)
	{
		int b = dmrSmsTxBuild(pdu, sizeof(pdu), hdr, "WX", PEER_ID, 262993U, 0, 0x1234, 0x91);
		static const uint8_t ip[20] = { 0x45, 0x00, 0x00, 0x2A, 0x12, 0x34, 0x00, 0x00, 0x40, 0x11, 0, 0,
				0x0C, 0x27, 0xD2, 0xD4, 0x0C, 0x04, 0x03, 0x51 };
		CHECK(b == 4, "WX: 4 blocks");
		CHECK(memcmp(pdu, ip, 10) == 0 && memcmp(&pdu[12], &ip[12], 8) == 0, "WX: IPv4 header");
		CHECK(hdr[0] == 0x02 && hdr[1] == (0x40 | 2) && hdr[8] == 0x84 && hdr[9] == 0x00, "WX: header %02X %02X %02X %02X", hdr[0], hdr[1], hdr[8], hdr[9]);
	}

	// CSBK preamble: BD 00, data follows (individual 80 / group C0), blocks to follow, addresses, CRC ^ A5A5
	{
		uint8_t c[12];
		dmrSmsTxPreamble(c, PEER_ID, TG, 1, 9);
		CHECK(c[0] == 0xBD && c[1] == 0x00 && c[2] == 0xC0 && c[3] == 9, "CSBK: group head");
		CHECK(c[4] == 0 && c[5] == 0x01 && c[6] == 0x04 && c[7] == (uint8_t)(PEER_ID >> 16) && c[9] == (uint8_t)PEER_ID, "CSBK: addresses");
		uint8_t h2[2];
		dmrSmsHdrCrc(c, h2); // ^ CCCC
		uint16_t raw = (uint16_t)(((h2[0] << 8) | h2[1]) ^ 0xCCCC);
		CHECK((uint16_t)(((c[10] << 8) | c[11]) ^ 0xA5A5) == raw, "CSBK: CRC mask A5A5");
		dmrSmsTxPreamble(c, PEER_ID, OUR_ID, 0, 0);
		CHECK(c[2] == 0x80 && c[3] == 0, "CSBK: individual");
	}

	// the receive buffers lent to a transmission
	{
		int bl = dmrSmsTxBuild(pdu, sizeof(pdu), hdr, "hello", PEER_ID, OUR_ID, 0, 7, 0x97);

		dmrSmsRxInit(&rx);
		CHECK(dmrSmsRxHold(&rx), "hold: idle receiver lends");
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, hdr, OUR_ID);
		CHECK(feedBlocks(pdu, bl * 12, 0, 0, -1) == 0 && rx.haveHeader == 0 && rx.ready == 0, "hold: bursts ignored");
		dmrSmsRxRelease(&rx);
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, hdr, OUR_ID);
		CHECK(feedBlocks(pdu, bl * 12, 0, 0, -1) == 1, "release: receives again");
		CHECK(dmrSmsRxHold(&rx) == false && rx.hold == 0, "hold: refused while a PDU waits for the main loop");
		rx.ready = 0;
		dmrSmsRxFeed(&rx, DMR_SMS_DT_DATA_HEADER, hdr, OUR_ID);
		feedBlocks(pdu, 12, 0, 0, -1);
		CHECK(dmrSmsRxHold(&rx) == false, "hold: refused while a PDU is coming in");
	}
}

int main(void)
{
	testTytCaptures();
	testIp();
	testRobustness();
	testShortData();
	testMapChar();
	testTx();

	if (failures)
	{
		printf("test_dmr_sms: %d FAILED\n", failures);
		return 1;
	}
	printf("test_dmr_sms: all passed\n");
	return 0;
}
