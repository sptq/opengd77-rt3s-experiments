/*
 * Fork: APRS receive - AX.25 / APRS parsing and geo helpers (no hardware
 * dependency, also compiled by the host test harness). See aprsRx.h.
 *
 * Position formats: uncompressed and compressed (!, =, /, @), objects (;),
 * items ()), Mic-E (' and `), with course/speed when present.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "functions/aprsRx.h"

#define DEG2RAD(x)   ((x) * (M_PI / 180.0))
#define RAD2DEG(x)   ((x) * (180.0 / M_PI))
#define KNOTS_TO_KMH 1.852

// --- AX.25 -------------------------------------------------------------------

static void parseAddress(const uint8_t *a, char *out, bool *repeated)
{
	uint32_t n = 0U;

	for (uint32_t i = 0U; i < 6U; i++)
	{
		char c = (char)(a[i] >> 1);
		if (c != ' ')
		{
			out[n++] = (((c >= 0x21) && (c <= 0x7E)) ? c : '?');
		}
	}

	uint8_t ssid = ((a[6] >> 1) & 0x0FU);
	if (ssid != 0U)
	{
		out[n++] = '-';
		if (ssid >= 10U)
		{
			out[n++] = '1';
			ssid -= 10U;
		}
		out[n++] = (char)('0' + ssid);
	}
	out[n] = 0;

	if (repeated)
	{
		*repeated = ((a[6] & 0x80U) != 0U);
	}
}

bool aprsRxParse(const aprsRxFrame_t *frame, aprsRxParsed_t *parsed)
{
	uint16_t pos = 0U;
	uint32_t addrCount = 0U;
	bool last = false;

	memset(parsed, 0, sizeof(aprsRxParsed_t));

	while ((last == false) && ((pos + 7U) <= frame->len))
	{
		const uint8_t *a = &frame->data[pos];
		bool repeated = false;

		last = ((a[6] & 0x01U) != 0U);

		if (addrCount == 0U)
		{
			parseAddress(a, parsed->dst, NULL);
			for (uint32_t i = 0U; i < 6U; i++)
			{
				parsed->dstRaw[i] = (char)(a[i] >> 1); // Mic-E needs the 6 raw characters
			}
			parsed->dstRaw[6] = 0;
		}
		else if (addrCount == 1U)
		{
			parseAddress(a, parsed->src, NULL);
		}
		else if (parsed->pathCount < APRS_RX_MAX_PATH)
		{
			char *p = parsed->path[parsed->pathCount++];
			parseAddress(a, p, &repeated);
			if (repeated)
			{
				strcat(p, "*");
			}
		}

		addrCount++;
		pos += 7U;
	}

	if ((last == false) || (addrCount < 2U))
	{
		return false;
	}

	// Control (+ PID for UI / I frames)
	if (pos < frame->len)
	{
		uint8_t ctrl = frame->data[pos++];
		if ((((ctrl & 0x01U) == 0U) || ((ctrl & 0xEFU) == 0x03U)) && (pos < frame->len))
		{
			pos++; // PID
		}
	}

	parsed->info = &frame->data[pos];
	parsed->infoLen = (uint16_t)(frame->len - pos);
	return true;
}

uint32_t aprsRxFormatTnc2(const aprsRxFrame_t *frame, char *out, uint32_t outSize)
{
	aprsRxParsed_t p;
	uint32_t n;

	if (outSize == 0U)
	{
		return 0U;
	}

	if (aprsRxParse(frame, &p) == false)
	{
		n = (uint32_t)snprintf(out, outSize, "(bad AX.25 header)");
		return ((n < outSize) ? n : (outSize - 1U));
	}

	n = (uint32_t)snprintf(out, outSize, "%s>%s", p.src, p.dst);
	for (uint32_t i = 0U; (i < p.pathCount) && (n < outSize); i++)
	{
		n += (uint32_t)snprintf(&out[n], (outSize - n), ",%s", p.path[i]);
	}

	if ((n + 1U) < outSize)
	{
		out[n++] = ':';
	}

	for (uint16_t i = 0U; (i < p.infoLen) && ((n + 1U) < outSize); i++)
	{
		char c = (char)p.info[i];
		out[n++] = (((c >= 0x20) && (c <= 0x7E)) ? c : '.');
	}

	if (n >= outSize)
	{
		n = (outSize - 1U);
	}
	out[n] = 0;
	return n;
}

// --- APRS positions ----------------------------------------------------------

static bool isDigitOrSpace(char c)
{
	return (((c >= '0') && (c <= '9')) || (c == ' '));
}

static int digitVal(char c)
{
	return ((c == ' ') ? 0 : (c - '0')); // ' ' = position ambiguity
}

// "DDMM.mmN/DDDMM.mmE>" + optional "CSE/SPD"
static bool parseUncompressed(const char *q, uint32_t len, aprsRxPosition_t *pos)
{
	if (len < 19U)
	{
		return false;
	}

	for (uint32_t i = 0U; i < 7U; i++)
	{
		if ((i != 4U) && (isDigitOrSpace(q[i]) == false))
		{
			return false;
		}
	}
	for (uint32_t i = 9U; i < 17U; i++)
	{
		if ((i != 14U) && (isDigitOrSpace(q[i]) == false))
		{
			return false;
		}
	}
	if ((q[4] != '.') || (q[14] != '.') || ((q[7] != 'N') && (q[7] != 'S')) || ((q[17] != 'E') && (q[17] != 'W')))
	{
		return false;
	}

	double lat = (digitVal(q[0]) * 10) + digitVal(q[1]) +
			(((digitVal(q[2]) * 10) + digitVal(q[3]) + (digitVal(q[5]) / 10.0) + (digitVal(q[6]) / 100.0)) / 60.0);
	double lon = (digitVal(q[9]) * 100) + (digitVal(q[10]) * 10) + digitVal(q[11]) +
			(((digitVal(q[12]) * 10) + digitVal(q[13]) + (digitVal(q[15]) / 10.0) + (digitVal(q[16]) / 100.0)) / 60.0);

	if ((lat > 90.0) || (lon > 180.0))
	{
		return false;
	}

	pos->lat = ((q[7] == 'S') ? -lat : lat);
	pos->lon = ((q[17] == 'W') ? -lon : lon);
	pos->symTable = q[8];
	pos->symCode = q[18];

	// Course/speed extension "ddd/sss" (knots)
	if ((len >= 26U) && (q[22] == '/'))
	{
		bool ok = true;
		for (uint32_t i = 19U; i < 26U; i++)
		{
			if ((i != 22U) && ((q[i] < '0') || (q[i] > '9')))
			{
				ok = false;
			}
		}
		if (ok)
		{
			int course = ((q[19] - '0') * 100) + ((q[20] - '0') * 10) + (q[21] - '0');
			int speed = ((q[23] - '0') * 100) + ((q[24] - '0') * 10) + (q[25] - '0');
			if ((course >= 1) && (course <= 360))
			{
				pos->courseDeg = (int16_t)(course % 360);
				pos->speedKmh = (int16_t)((speed * KNOTS_TO_KMH) + 0.5);
			}
		}
	}
	return true;
}

static bool isBase91(char c)
{
	return ((c >= '!') && (c <= '{'));
}

// "/YYYYXXXX$csT"
static bool parseCompressed(const char *q, uint32_t len, aprsRxPosition_t *pos)
{
	if (len < 13U)
	{
		return false;
	}

	char t = q[0];
	if (((t == '/') || (t == '\\') || ((t >= 'A') && (t <= 'Z')) || ((t >= 'a') && (t <= 'j'))) == false)
	{
		return false;
	}

	for (uint32_t i = 1U; i < 9U; i++)
	{
		if (isBase91(q[i]) == false)
		{
			return false;
		}
	}

	double y = 0.0, x = 0.0;
	for (uint32_t i = 0U; i < 4U; i++)
	{
		y = (y * 91.0) + (q[1 + i] - 33);
		x = (x * 91.0) + (q[5 + i] - 33);
	}

	pos->lat = 90.0 - (y / 380926.0);
	pos->lon = -180.0 + (x / 190463.0);
	pos->symTable = t;
	pos->symCode = q[9];

	if ((pos->lat < -90.0) || (pos->lat > 90.0) || (pos->lon < -180.0) || (pos->lon > 180.0))
	{
		return false;
	}

	char c = q[10], s = q[11], type = q[12];
	if ((c != ' ') && isBase91(c) && isBase91(s) && isBase91(type) && ((((type - 33) & 0x18) != 0x10)) && ((c - 33) <= 89))
	{
		pos->courseDeg = (int16_t)(((c - 33) * 4) % 360);
		pos->speedKmh = (int16_t)(((pow(1.08, (s - 33)) - 1.0) * KNOTS_TO_KMH) + 0.5);
	}
	return true;
}

static bool parsePositionAt(const char *q, uint32_t len, aprsRxPosition_t *pos)
{
	if (len == 0U)
	{
		return false;
	}
	if (isDigitOrSpace(q[0]))
	{
		return parseUncompressed(q, len, pos);
	}
	return parseCompressed(q, len, pos);
}

// Mic-E: latitude / flags in the destination address, longitude / speed / course in the info field
static bool parseMicE(const aprsRxParsed_t *p, const char *info, uint32_t len, aprsRxPosition_t *pos)
{
	const char *d = p->dstRaw;
	int digits[6];

	if (len < 9U)
	{
		return false;
	}

	for (uint32_t i = 0U; i < 6U; i++)
	{
		char c = d[i];
		if ((c >= '0') && (c <= '9'))
		{
			digits[i] = (c - '0');
		}
		else if ((c >= 'A') && (c <= 'J'))
		{
			digits[i] = (c - 'A');
		}
		else if ((c >= 'P') && (c <= 'Y'))
		{
			digits[i] = (c - 'P');
		}
		else if ((c == 'K') || (c == 'L') || (c == 'Z'))
		{
			digits[i] = 0; // ambiguity
		}
		else
		{
			return false;
		}
	}

	bool north = ((d[3] >= 'P') && (d[3] <= 'Z'));
	bool lonOffset = ((d[4] >= 'P') && (d[4] <= 'Z'));
	bool west = ((d[5] >= 'P') && (d[5] <= 'Z'));

	double lat = (digits[0] * 10) + digits[1] + ((((digits[2] * 10) + digits[3]) + (((digits[4] * 10) + digits[5]) / 100.0)) / 60.0);

	int lonDeg = ((uint8_t)info[1] - 28);
	if (lonOffset)
	{
		lonDeg += 100;
	}
	if ((lonDeg >= 180) && (lonDeg <= 189))
	{
		lonDeg -= 80;
	}
	else if ((lonDeg >= 190) && (lonDeg <= 199))
	{
		lonDeg -= 190;
	}

	int lonMin = ((uint8_t)info[2] - 28);
	if (lonMin >= 60)
	{
		lonMin -= 60;
	}
	int lonHun = ((uint8_t)info[3] - 28);

	if ((lat > 90.0) || (lonDeg < 0) || (lonDeg > 179) || (lonMin < 0) || (lonHun < 0) || (lonHun > 99))
	{
		return false;
	}

	double lon = lonDeg + ((lonMin + (lonHun / 100.0)) / 60.0);

	pos->lat = (north ? lat : -lat);
	pos->lon = (west ? -lon : lon);
	pos->symCode = info[7];
	pos->symTable = info[8];

	int sp = ((uint8_t)info[4] - 28);
	int dc = ((uint8_t)info[5] - 28);
	int se = ((uint8_t)info[6] - 28);
	if ((sp >= 0) && (dc >= 0) && (se >= 0))
	{
		int speed = ((sp * 10) + (dc / 10));
		int course = (((dc % 10) * 100) + se);
		if (speed >= 800)
		{
			speed -= 800;
		}
		if (course >= 400)
		{
			course -= 400;
		}
		if ((course >= 0) && (course <= 360))
		{
			pos->courseDeg = (int16_t)(course % 360);
			pos->speedKmh = (int16_t)((speed * KNOTS_TO_KMH) + 0.5);
		}
	}
	return true;
}

static void copyTrimmed(char *dst, const char *src, uint32_t len)
{
	while ((len > 0U) && (src[len - 1U] == ' '))
	{
		len--;
	}
	if (len >= APRS_RX_CALL_LEN)
	{
		len = (APRS_RX_CALL_LEN - 1U);
	}
	memcpy(dst, src, len);
	dst[len] = 0;
}

bool aprsRxParsePosition(const aprsRxParsed_t *p, aprsRxPosition_t *pos)
{
	const char *info = (const char *)p->info;
	uint32_t len = p->infoLen;

	memset(pos, 0, sizeof(aprsRxPosition_t));
	pos->courseDeg = -1;
	pos->speedKmh = -1;
	strcpy(pos->name, p->src);

	if (len < 2U)
	{
		return false;
	}

	switch (info[0])
	{
		case '!':
		case '=':
			return parsePositionAt(&info[1], (len - 1U), pos);

		case '/':
		case '@':
			return ((len > 8U) && parsePositionAt(&info[8], (len - 8U), pos)); // 7 chars timestamp

		case ';': // object: 9 chars name, '*' / '_', 7 chars timestamp
			if ((len < 19U) || ((info[10] != '*') && (info[10] != '_')))
			{
				return false;
			}
			copyTrimmed(pos->name, &info[1], 9U);
			pos->killed = (info[10] == '_');
			return parsePositionAt(&info[18], (len - 18U), pos);

		case ')': // item: 3..9 chars name, '!' / '_'
		{
			for (uint32_t i = 4U; (i <= 10U) && (i < len); i++)
			{
				if ((info[i] == '!') || (info[i] == '_'))
				{
					copyTrimmed(pos->name, &info[1], (i - 1U));
					pos->killed = (info[i] == '_');
					return parsePositionAt(&info[i + 1U], (len - i - 1U), pos);
				}
			}
			return false;
		}

		case '\'':
		case '`':
		case 0x1C:
		case 0x1D:
			return parseMicE(p, info, len, pos);

		default:
			break;
	}

	return false;
}

// --- Messages -------------------------------------------------------------------
// ":ADDRESSEE:text{ID" / ":ADDRESSEE:ackID" / ":ADDRESSEE:rejID" (reply-acks "{MM}AA" too)
bool aprsRxParseMessage(const aprsRxParsed_t *p, aprsRxMessage_t *msg)
{
	const char *info = (const char *)p->info;
	uint32_t len = p->infoLen;

	memset(msg, 0, sizeof(aprsRxMessage_t));

	if ((len < 11U) || (info[0] != ':') || (info[10] != ':'))
	{
		return false;
	}

	copyTrimmed(msg->addressee, &info[1], 9U);
	if (msg->addressee[0] == 0)
	{
		return false;
	}

	const char *t = &info[11];
	uint32_t tLen = (len - 11U);

	// strip CR / LF / spaces at the end
	while ((tLen > 0U) && ((t[tLen - 1U] == '\r') || (t[tLen - 1U] == '\n') || (t[tLen - 1U] == ' ')))
	{
		tLen--;
	}

	if ((tLen >= 4U) && (tLen <= 3U + 5U + 4U) && ((strncmp(t, "ack", 3) == 0) || (strncmp(t, "rej", 3) == 0)))
	{
		uint32_t n = 0U;

		msg->isAck = (t[0] == 'a');
		msg->isRej = (t[0] == 'r');
		for (uint32_t i = 3U; (i < tLen) && (t[i] != '}') && (n < (APRS_RX_MSG_ID_LEN - 1U)); i++)
		{
			msg->msgId[n++] = t[i];
		}
		msg->msgId[n] = 0;
		return (n > 0U);
	}

	// message number after the last '{'
	for (int32_t i = (int32_t)tLen - 1; (i >= 0) && (i >= ((int32_t)tLen - 1 - 8)); i--)
	{
		if (t[i] == '{')
		{
			uint32_t n = 0U;

			for (uint32_t j = (uint32_t)i + 1U; (j < tLen) && (t[j] != '}') && (n < (APRS_RX_MSG_ID_LEN - 1U)); j++)
			{
				msg->msgId[n++] = t[j];
			}
			msg->msgId[n] = 0;
			tLen = (uint32_t)i;
			break;
		}
	}

	uint32_t n = 0U;
	for (uint32_t i = 0U; (i < tLen) && (n < (APRS_RX_MSG_TEXT_LEN - 1U)); i++)
	{
		char c = t[i];
		msg->text[n++] = (((c >= 0x20) && (c <= 0x7E)) ? c : '.');
	}
	msg->text[n] = 0;

	return true;
}

// --- Geo ----------------------------------------------------------------------

double aprsRxDistanceKm(double lat1, double lon1, double lat2, double lon2)
{
	double dLat = DEG2RAD(lat2 - lat1);
	double dLon = DEG2RAD(lon2 - lon1);
	double a = (sin(dLat / 2.0) * sin(dLat / 2.0)) + (cos(DEG2RAD(lat1)) * cos(DEG2RAD(lat2)) * sin(dLon / 2.0) * sin(dLon / 2.0));

	return (6371.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
}

double aprsRxBearingDeg(double lat1, double lon1, double lat2, double lon2)
{
	double y = sin(DEG2RAD(lon2 - lon1)) * cos(DEG2RAD(lat2));
	double x = (cos(DEG2RAD(lat1)) * sin(DEG2RAD(lat2))) - (sin(DEG2RAD(lat1)) * cos(DEG2RAD(lat2)) * cos(DEG2RAD(lon2 - lon1)));
	double b = RAD2DEG(atan2(y, x));

	return ((b < 0.0) ? (b + 360.0) : b);
}

void aprsRxFormatDistance(double km, char *buf, uint32_t size)
{
	if (km < 1.0)
	{
		snprintf(buf, size, "%um", (unsigned)((km * 1000.0) + 0.5));
	}
	else if (km < 100.0)
	{
		unsigned t = (unsigned)((km * 10.0) + 0.5);
		snprintf(buf, size, "%u.%ukm", (t / 10U), (t % 10U));
	}
	else
	{
		snprintf(buf, size, "%ukm", (unsigned)(km + 0.5));
	}
}
