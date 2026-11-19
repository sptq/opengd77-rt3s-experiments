/*
 * Fork: grid conversions for the field tools (SP7TEAM firmware, Marcin SP7TQ).
 *
 * WGS84 latitude / longitude to UTM and MGRS (the grid used by armies, NATO and
 * search & rescue teams). Hardware independent, also built on the host for tests.
 * Latitudes outside -80..84 (UPS zones) are not supported.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_FIELD_NAV_H_
#define _OPENGD77_FIELD_NAV_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
	int    zone;       // 1..60
	char   band;       // latitude band letter C..X
	double easting;    // metres
	double northing;   // metres (southern hemisphere: + 10 000 km)
} fieldUtm_t;

bool fieldNavToUtm(double lat, double lon, fieldUtm_t *utm);

// "34U EC 00833 86586" (1 m precision), false outside the UTM area
bool fieldNavToMgrs(double lat, double lon, char *buf, size_t size);

// 16-point compass name of a bearing in degrees ("N", "NNE", ...)
const char *fieldNavCompass(double bearing);

#endif
