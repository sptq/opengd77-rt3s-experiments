/*
 * Fork: GPS trip computer (RAM only).
 *
 * Accumulates distance, moving time, max speed and altitude gain/loss from the
 * NMEA RMC/GGA data, in the background (whatever screen is shown).
 * Nothing is written to flash; everything is lost on power off.
 */
#ifndef _OPENGD77_GPSTRIP_H_
#define _OPENGD77_GPSTRIP_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
#define GPS_TRIP_COMPUTER

typedef struct
{
	uint32_t distanceM;          // travelled distance, metres
	uint32_t movingTimeMs;       // time spent above the moving threshold
	uint32_t elapsedTimeMs;      // time since the first fix of this trip
	uint16_t maxSpeedHundredthKn;
	uint16_t altGainM;
	uint16_t altLossM;
	bool     started;            // at least one fix has been seen since reset
} gpsTrip_t;

extern gpsTrip_t gpsTrip;

void gpsTripReset(void);
// Called by the NMEA parser after each RMC sentence with a valid fix.
void gpsTripUpdate(void);
// Average moving speed in hundredth of km/h (0 when not moved yet).
uint32_t gpsTripAvgSpeedHundredthKmh(void);

#endif

#endif /* _OPENGD77_GPSTRIP_H_ */
