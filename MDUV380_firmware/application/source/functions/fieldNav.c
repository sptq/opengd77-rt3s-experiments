/*
 * Fork: WGS84 to UTM / MGRS (see functions/fieldNav.h).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include <stdio.h>
#include "functions/fieldNav.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WGS84_A   6378137.0
#define WGS84_F   (1.0 / 298.257223563)
#define UTM_K0    0.9996

bool fieldNavToUtm(double lat, double lon, fieldUtm_t *utm)
{
	if ((lat < -80.0) || (lat > 84.0) || (lon < -180.0) || (lon > 180.0))
	{
		return false;
	}

	if (lon >= 180.0)
	{
		lon -= 360.0;
	}

	int zone = (int)floor((lon + 180.0) / 6.0) + 1;

	// Norway and Svalbard exceptions
	if ((lat >= 56.0) && (lat < 64.0) && (lon >= 3.0) && (lon < 12.0))
	{
		zone = 32;
	}
	else if ((lat >= 72.0) && (lat <= 84.0) && (lon >= 0.0) && (lon < 42.0))
	{
		zone = ((lon < 9.0) ? 31 : ((lon < 21.0) ? 33 : ((lon < 33.0) ? 35 : 37)));
	}

	static const char bands[] = "CDEFGHJKLMNPQRSTUVWX";
	int b = (int)floor((lat + 80.0) / 8.0);
	if (b > 19)
	{
		b = 19; // X goes up to 84
	}

	double e2 = (WGS84_F * (2.0 - WGS84_F));
	double e4 = (e2 * e2);
	double e6 = (e4 * e2);
	double ep2 = (e2 / (1.0 - e2));
	double phi = (lat * (M_PI / 180.0));
	double lambda0 = ((((zone - 1) * 6) - 180 + 3) * (M_PI / 180.0));
	double sinPhi = sin(phi), cosPhi = cos(phi), tanPhi = tan(phi);
	double N = (WGS84_A / sqrt(1.0 - (e2 * sinPhi * sinPhi)));
	double T = (tanPhi * tanPhi);
	double C = (ep2 * cosPhi * cosPhi);
	double A = (cosPhi * ((lon * (M_PI / 180.0)) - lambda0));
	double M = (WGS84_A * (((1.0 - (e2 / 4.0) - ((3.0 * e4) / 64.0) - ((5.0 * e6) / 256.0)) * phi)
			- ((((3.0 * e2) / 8.0) + ((3.0 * e4) / 32.0) + ((45.0 * e6) / 1024.0)) * sin(2.0 * phi))
			+ ((((15.0 * e4) / 256.0) + ((45.0 * e6) / 1024.0)) * sin(4.0 * phi))
			- (((35.0 * e6) / 3072.0) * sin(6.0 * phi))));
	double A2 = (A * A), A3 = (A2 * A), A4 = (A3 * A), A5 = (A4 * A), A6 = (A5 * A);

	utm->zone = zone;
	utm->band = bands[b];
	utm->easting = ((UTM_K0 * N * (A + (((1.0 - T + C) * A3) / 6.0) + (((5.0 - (18.0 * T) + (T * T) + (72.0 * C) - (58.0 * ep2)) * A5) / 120.0))) + 500000.0);
	utm->northing = (UTM_K0 * (M + (N * tanPhi * ((A2 / 2.0) + (((5.0 - T + (9.0 * C) + (4.0 * C * C)) * A4) / 24.0)
			+ (((61.0 - (58.0 * T) + (T * T) + (600.0 * C) - (330.0 * ep2)) * A6) / 720.0)))));
	if (lat < 0.0)
	{
		utm->northing += 10000000.0;
	}
	return true;
}

bool fieldNavToMgrs(double lat, double lon, char *buf, size_t size)
{
	static const char *const columnSets[3] = { "ABCDEFGH", "JKLMNPQR", "STUVWXYZ" };
	static const char rowLetters[] = "ABCDEFGHJKLMNPQRSTUV";
	fieldUtm_t utm;

	if (fieldNavToUtm(lat, lon, &utm) == false)
	{
		return false;
	}

	// Truncate (not round) to the metre, as MGRS does
	uint32_t e = (uint32_t)floor(utm.easting);
	uint32_t n = (uint32_t)floor(utm.northing);
	int set = ((utm.zone - 1) % 3);
	int col = (int)(e / 100000U) - 1;
	int row = (int)(((n / 100000U) + (((utm.zone % 2) == 0) ? 5U : 0U)) % 20U);

	if ((col < 0) || (col > 7))
	{
		return false;
	}

	snprintf(buf, size, "%d%c %c%c %05u %05u", utm.zone, utm.band, columnSets[set][col], rowLetters[row],
			(unsigned)(e % 100000U), (unsigned)(n % 100000U));
	return true;
}

const char *fieldNavCompass(double bearing)
{
	static const char *const names[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
	int i = (int)floor((fmod((bearing + 360.0), 360.0) + 11.25) / 22.5);

	return names[i % 16];
}
