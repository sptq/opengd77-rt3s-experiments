// Host test for aprsRxParse.c
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "functions/aprsRx.h"

static int fails;

static void addr(uint8_t *p, const char *call, int ssid, int last, int h)
{
	int i = 0;
	for (; i < 6 && call[i]; i++) p[i] = (uint8_t)(call[i] << 1);
	for (; i < 6; i++) p[i] = ' ' << 1;
	p[6] = (uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0) | (h ? 0x80 : 0));
}

static void mkFrame(aprsRxFrame_t *f, const char *src, int ssid, const char *dst, const char *info, int infoLen)
{
	int n = 0;
	addr(f->data + n, dst, 0, 0, 0); n += 7;
	addr(f->data + n, src, ssid, 0, 0); n += 7;
	addr(f->data + n, "WIDE1", 1, 1, 1); n += 7;
	f->data[n++] = 0x03; f->data[n++] = 0xF0;
	memcpy(f->data + n, info, infoLen); n += infoLen;
	f->len = (uint16_t)n;
}

static void check(const char *name, const char *dst, const char *info, int infoLen, int expectPos, double lat, double lon, int course, int speedKmh, const char *expName)
{
	aprsRxFrame_t f; aprsRxParsed_t p; aprsRxPosition_t pos;
	mkFrame(&f, "SP7TQ", 9, dst, info, infoLen);
	int ok = aprsRxParse(&f, &p);
	int hp = ok && aprsRxParsePosition(&p, &pos);
	int good = ok && (hp == expectPos);
	if (good && expectPos)
	{
		good = (fabs(pos.lat - lat) < 0.0005) && (fabs(pos.lon - lon) < 0.0005) &&
				(course == -2 || pos.courseDeg == course) && (speedKmh == -2 || abs(pos.speedKmh - speedKmh) <= 1) &&
				(expName == NULL || strcmp(pos.name, expName) == 0);
	}
	printf("%-4s %-28s pos=%d lat=%.4f lon=%.4f crs=%d spd=%d name=%s\n", good ? "OK" : "FAIL", name, hp, hp ? pos.lat : 0, hp ? pos.lon : 0, hp ? pos.courseDeg : 0, hp ? pos.speedKmh : 0, hp ? pos.name : "-");
	if (!good) fails++;
}

// Mic-E encoder written from the APRS 1.0.1 spec tables (independent of the decoder)
static void micE(double lat, double lon, int speedKn, int course, char *dst, char *info)
{
	int north = lat >= 0, west = lon < 0;
	lat = fabs(lat); lon = fabs(lon);
	int latDeg = (int)lat; double latMin = (lat - latDeg) * 60.0;
	int latMinI = (int)latMin; int latHun = (int)lround((latMin - latMinI) * 100); if (latHun == 100) { latHun = 0; latMinI++; }
	int d[6] = { latDeg / 10, latDeg % 10, latMinI / 10, latMinI % 10, latHun / 10, latHun % 10 };
	int lonDeg = (int)lon; double lonMin = (lon - lonDeg) * 60.0;
	int lonMinI = (int)lonMin; int lonHun = (int)lround((lonMin - lonMinI) * 100); if (lonHun == 100) { lonHun = 0; lonMinI++; }
	int offset = (lonDeg >= 100 || lonDeg <= 9);
	for (int i = 0; i < 6; i++)
	{
		int custom = (i == 3 && north) || (i == 4 && offset) || (i == 5 && west) || (i < 3); // msg bits 1 (std) for 0..2
		dst[i] = (char)(custom ? ('P' + d[i]) : ('0' + d[i]));
	}
	dst[6] = 0;
	int dd;
	if (lonDeg <= 9) dd = lonDeg + 118;           // 0-9   -> 118-127
	else if (lonDeg <= 99) dd = lonDeg + 28;      // 10-99 -> 38-127
	else if (lonDeg <= 109) dd = lonDeg + 8;      // 100-109 -> 108-117
	else dd = lonDeg - 100 + 28;                  // 110-179 -> 38-107
	int mm = (lonMinI < 10) ? lonMinI + 88 : lonMinI + 28;
	info[0] = '`'; info[1] = (char)dd; info[2] = (char)mm; info[3] = (char)(lonHun + 28);
	int sp = speedKn / 10, dc = (speedKn % 10) * 10 + course / 100, se = course % 100;
	info[4] = (char)(sp + 28); info[5] = (char)(dc + 28); info[6] = (char)(se + 28);
	info[7] = '>'; info[8] = '/'; strcpy(info + 9, "]Test");
}

int main(void)
{
	// TNC2 formatting
	aprsRxFrame_t f; char line[300];
	mkFrame(&f, "SP7TQ", 9, "APRS", "!5146.00N/01928.00E>hello", 25);
	aprsRxFormatTnc2(&f, line, sizeof(line));
	int t = strcmp(line, "SP7TQ-9>APRS,WIDE1-1*:!5146.00N/01928.00E>hello") == 0;
	printf("%-4s tnc2: %s\n", t ? "OK" : "FAIL", line); if (!t) fails++;

	// Spec examples
	check("uncompressed", "APRS", "!4903.50N/07201.75W-Test", 24, 1, 49.058333, -72.029167, -1, -1, "SP7TQ-9");
	check("uncompressed + crs/spd", "APRS", "=4903.50N/07201.75W>088/036", 27, 1, 49.058333, -72.029167, 88, 67, NULL);
	check("timestamp @", "APRS", "@092345z4903.50N/07201.75W>", 27, 1, 49.058333, -72.029167, -1, -1, NULL);
	check("ambiguity", "APRS", "!4903.  N/07201.  W-", 20, 1, 49.05, -72.016667, -1, -1, NULL);
	check("compressed spec", "APRS", "=/5L!!<*e7>7P[", 14, 1, 49.5, -72.75, 88, 67, NULL);
	check("object", "APRS", ";LEADER   *092345z4903.50N/07201.75W>088/036", 45, 1, 49.058333, -72.029167, 88, 67, "LEADER");
	check("item", "APRS", ")AID #2!4903.50N/07201.75WA", 27, 1, 49.058333, -72.029167, -1, -1, "AID #2");
	check("status (no position)", "APRS", ">Net tonight 20:00", 18, 0, 0, 0, 0, 0, NULL);
	check("message (no position)", "APRS", ":SP7TQ    :hi{1", 15, 0, 0, 0, 0, 0, NULL);

	// Mic-E round trips
	struct { double lat, lon; int sp, crs; } m[] = {
		{ 51.7667, 19.4667, 36, 88 }, { -33.4273, -112.129, 20, 251 }, { 52.2297, 21.0122, 0, 0 },
		{ 40.4168, -3.7038, 120, 359 }, { 64.1466, -21.9426, 5, 180 }, { 1.2903, 103.8519, 15, 45 },
	};
	for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++)
	{
		char dst[8], info[32], name[40];
		micE(m[i].lat, m[i].lon, m[i].sp, m[i].crs, dst, info);
		snprintf(name, sizeof(name), "mic-e %.2f,%.2f", m[i].lat, m[i].lon);
		check(name, dst, info, (int)strlen(info + 9) + 9, 1, m[i].lat, m[i].lon, m[i].crs % 360, (int)lround(m[i].sp * 1.852), NULL);
	}

	// Geo
	double d = aprsRxDistanceKm(51.7592, 19.4560, 52.2297, 21.0122); // Lodz -> Warsaw ~ 119 km
	double b = aprsRxBearingDeg(51.7592, 19.4560, 52.2297, 21.0122);  // ~ 63 deg
	int g = (d > 117 && d < 121) && (b > 60 && b < 66);
	char dist[12]; aprsRxFormatDistance(d, dist, sizeof(dist));
	printf("%-4s geo: %.1f km %s  bearing %.1f\n", g ? "OK" : "FAIL", d, dist, b); if (!g) fails++;

	printf(fails ? "FAILED: %d\n" : "ALL PASSED\n", fails);
	return fails;
}
