/*
 * Fork: GPS trip computer (RAM only). See gpsTrip.h.
 *
 * This file is part of an experimental OpenGD77 fork and follows the
 * OpenGD77 licence (non-commercial use only).
 */
#include <string.h>
#include "main.h"
#include "functions/gpsTrip.h"

#if defined(GPS_TRIP_COMPUTER)
#include "functions/ticks.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/gps.h"

// Below this ground speed the receiver is considered stationary. A bit above
// GPS_SPEED_THRESHOLD_MIN (1 km/h) so position jitter while standing still
// does not slowly add distance.
#define TRIP_MOVING_SPEED_MIN_HUNDREDTH_KN   108U  // ~2 km/h
// Gaps between two fixes longer than this (TX, GPS dropout) are not counted
// as moving time and restart the distance anchor.
#define TRIP_MAX_GAP_MS                      10000U
// Reject position jumps implying more than this (glitches after reacquiring).
#define TRIP_MAX_PLAUSIBLE_SPEED_MPS         100.0
// Altitude hysteresis, the GPS height is noisy.
#define TRIP_ALT_HYSTERESIS_M                5

gpsTrip_t gpsTrip;

static double   tripDistanceAccumM;
static double   anchorLat;
static double   anchorLon;
static bool     anchorValid;
static uint32_t lastUpdateMs;
static uint32_t startMs;
static int16_t  altRefM;
static bool     altRefValid;

void gpsTripReset(void)
{
	memset(&gpsTrip, 0, sizeof(gpsTrip));
	tripDistanceAccumM = 0.0;
	anchorValid = false;
	altRefValid = false;
	lastUpdateMs = 0U;
	startMs = 0U;
}

static void gpsTripUpdateAltitude(void)
{
	if (((gpsData.Status & GPS_STATUS_HAS_HEIGHT) == 0) || ((gpsData.Status & GPS_STATUS_3D_FIX) == 0))
	{
		return;
	}

	int16_t h = gpsData.HeightInM;

	if (altRefValid == false)
	{
		altRefM = h;
		altRefValid = true;
		return;
	}

	int32_t diff = (int32_t)h - (int32_t)altRefM;

	if (diff >= TRIP_ALT_HYSTERESIS_M)
	{
		gpsTrip.altGainM += (uint16_t)diff;
		altRefM = h;
	}
	else if (diff <= -TRIP_ALT_HYSTERESIS_M)
	{
		gpsTrip.altLossM += (uint16_t)(-diff);
		altRefM = h;
	}
}

void gpsTripUpdate(void)
{
	if ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) != (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION))
	{
		return;
	}

	uint32_t now = ticksGetMillis();
	double lat = gpsData.LatitudeHiRes;
	double lon = gpsData.LongitudeHiRes;

	if (gpsTrip.started == false)
	{
		gpsTrip.started = true;
		startMs = now;
		lastUpdateMs = now;
		anchorLat = lat;
		anchorLon = lon;
		anchorValid = true;
		gpsTripUpdateAltitude();
		return;
	}

	uint32_t dt = now - lastUpdateMs;
	lastUpdateMs = now;
	gpsTrip.elapsedTimeMs = now - startMs;

	gpsTripUpdateAltitude();

	if (dt > TRIP_MAX_GAP_MS)
	{
		// Too long since the previous fix: restart the anchor, count nothing.
		anchorLat = lat;
		anchorLon = lon;
		anchorValid = true;
		return;
	}

	bool moving = ((gpsData.Status & GPS_STATUS_HAS_SPEED) && (gpsData.SpeedInHundredthKn >= TRIP_MOVING_SPEED_MIN_HUNDREDTH_KN));

	if (moving == false)
	{
		// Keep the anchor where we stopped, so jitter is not accumulated.
		return;
	}

	if (anchorValid)
	{
		double d = distanceBetweenTwoCoords(anchorLat, anchorLon, lat, lon, false); // metres

		if ((dt > 0U) && (d <= (TRIP_MAX_PLAUSIBLE_SPEED_MPS * ((double)dt / 1000.0))))
		{
			tripDistanceAccumM += d;
			gpsTrip.distanceM = (uint32_t)tripDistanceAccumM;
			gpsTrip.movingTimeMs += dt;

			if (gpsData.SpeedInHundredthKn > gpsTrip.maxSpeedHundredthKn)
			{
				gpsTrip.maxSpeedHundredthKn = gpsData.SpeedInHundredthKn;
			}
		}
	}

	anchorLat = lat;
	anchorLon = lon;
	anchorValid = true;
}

uint32_t gpsTripAvgSpeedHundredthKmh(void)
{
	if (gpsTrip.movingTimeMs < 1000U)
	{
		return 0U;
	}

	// m / ms * 3600 = km/h ; * 100 for hundredth
	return (uint32_t)(((uint64_t)gpsTrip.distanceM * 360000ULL) / gpsTrip.movingTimeMs);
}

#endif
