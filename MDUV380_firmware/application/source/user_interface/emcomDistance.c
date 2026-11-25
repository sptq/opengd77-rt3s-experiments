/*
 * Pure helpers for the EMCOM channel-distance display.
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "user_interface/emcomDistance.h"

bool emcomCoordinateIsValid(double latitude, double longitude)
{
	return (isfinite(latitude) && isfinite(longitude) &&
			(latitude >= -90.0) && (latitude <= 90.0) &&
			(longitude >= -180.0) && (longitude <= 180.0));
}

/*
 * Format a validated great-circle result without changing APRS formatting.
 * Rounding is performed before selecting the display unit so values that
 * cross 1 km or 100 km do not produce "1000m" or "100.0km".
 */
bool emcomFormatDistanceMeters(double meters, char *buf, uint32_t size)
{
	uint32_t roundedMeters;

	if (size == 0U)
	{
		return false;
	}

	if (buf == NULL)
	{
		return false;
	}

	snprintf(buf, size, "--");

	if ((isfinite(meters) == false) || (meters < 0.0) || (meters > ((double)UINT32_MAX - 0.5)))
	{
		return false;
	}

	roundedMeters = (uint32_t)(meters + 0.5);

	if (roundedMeters < 1000U)
	{
		snprintf(buf, size, "%um", (unsigned)roundedMeters);
	}
	else if (roundedMeters < 100000U)
	{
		uint32_t tenthsKm = ((roundedMeters + 50U) / 100U);

		if (tenthsKm >= 1000U)
		{
			snprintf(buf, size, "100km");
		}
		else
		{
			snprintf(buf, size, "%u.%ukm", (unsigned)(tenthsKm / 10U), (unsigned)(tenthsKm % 10U));
		}
	}
	else
	{
		uint32_t wholeKm = (roundedMeters / 1000U);

		if ((roundedMeters % 1000U) >= 500U)
		{
			wholeKm++;
		}
		snprintf(buf, size, "%ukm", (unsigned)wholeKm);
	}

	return true;
}
