/*
 * Fork: field operations data in the SPI flash (SP7TEAM firmware, Marcin SP7TQ).
 *
 * 64 KB at the end of the 16 MB flash (taken from the end of the GPS NMEA log):
 *   sector 0     : 0x000 PACE communication plan (Primary / Alternate / Contingency / Emergency)
 *                  0x400 APRS canned replies (12 x 72 bytes, functions/aprsText.h)
 *                  0x800 saved waypoints (8 x 24 bytes)
 *                  0x900 comms schedules / skeds (4 x 16 bytes)
 *                  Records have their own magic: the rest of sector 0 may still hold old
 *                  GPS log bytes (it is only ever rewritten by read-modify-write), so a slot
 *                  without the magic or out of range reads as empty.
 *   sectors 1..15: coverage survey points (16 bytes each, filled linearly, 3840 points)
 *
 * Nothing is kept in RAM: every access reads the flash.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_FIELD_DATA_H_
#define _OPENGD77_FIELD_DATA_H_

#include <stdint.h>
#include <stdbool.h>
#include "functions/skedMath.h"

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD9600)
#define HAS_FIELD_TOOLS

#define FIELD_DATA_FLASH_ADDRESS   ((16U * 1024U * 1024U) - (64U * 1024U)) // the GPS NMEA log ends here
#define FIELD_DATA_FLASH_SIZE      (64U * 1024U)

// --- PACE plan ---------------------------------------------------------------------------
#define PACE_SLOTS                 4

typedef struct
{
	int16_t zoneNumber;     // zone list position (codeplugZoneGetDataForNumber), -1: empty slot
	int16_t channelNumber;  // codeplug channel index (1..1024)
} paceSlot_t;

bool fieldPaceGet(int slot, paceSlot_t *data);  // false: empty
void fieldPaceSet(int slot, const paceSlot_t *data); // NULL clears the slot
bool paceActivateChannel(const paceSlot_t *data); // menuPACE.c: channel mode on that channel, false: deleted / empty

// --- coverage survey -----------------------------------------------------------------------
#define COVERAGE_FLAG_FIX          0x01U  // position from a GPS fix (else no position)
#define COVERAGE_FLAG_CARRIER      0x02U  // signal present (RX LED / FM audio)
#define COVERAGE_FLAG_DMR          0x04U  // channel in DMR mode
#define COVERAGE_FLAG_SESSION      0x08U  // first point of a recording session

typedef struct
{
	uint32_t time;     // UTC seconds, 0 = no clock; 0xFFFFFFFF = empty
	int32_t  lat;      // degrees * 1e6
	int32_t  lon;      // degrees * 1e6
	int16_t  dBm;
	uint8_t  flags;
	uint8_t  freqKHz10; // RX frequency / 10 kHz, low byte (just a hint for the export)
} coveragePoint_t;

#define COVERAGE_POINTS_MAX        ((FIELD_DATA_FLASH_SIZE - 4096U) / sizeof(coveragePoint_t))

uint32_t fieldCoverageCount(void);

// --- APRS canned replies (Options > APRS > Replies, sent from the APRS RX screen) ----------
#define FIELD_APRS_REPLIES_OFFSET  0x400U
// 'text': APRS_TEXT_MSG_MAX + 1 bytes. Returns true when the slot was edited (else the default).
bool fieldAprsReplyGet(int slot, char *text);
void fieldAprsReplySet(int slot, const char *text); // NULL / empty: back to the default

// --- saved waypoints (GPS > Waypoint > '*', quickkey "Waypoints") ----------------------
#define FIELD_WAYPOINTS_OFFSET     0x800U
#define FIELD_WAYPOINT_SLOTS       8
#define FIELD_WAYPOINT_NAME_LEN    8
#define FIELD_WAYPOINT_FLAG_GPS    0x01U  // saved from a GPS fix
#define FIELD_WAYPOINT_FLAG_KEYPAD 0x02U  // typed in
#define FIELD_WAYPOINT_FLAG_APRS   0x04U  // an APRS station position

typedef struct
{
	uint32_t magic;
	int32_t  lat;      // degrees * 1e6
	int32_t  lon;      // degrees * 1e6
	char     name[FIELD_WAYPOINT_NAME_LEN]; // NUL padded, not terminated when 8 chars long
	uint8_t  flags;
	uint8_t  reserved[3];
} fieldWaypoint_t;

bool fieldWaypointGet(int slot, fieldWaypoint_t *wp); // false: empty
void fieldWaypointSet(int slot, const fieldWaypoint_t *wp); // NULL deletes
// First free slot, saved with 'name' (NULL: "WP<n>"). Returns the slot, -1 when all 8 are used.
int fieldWaypointAdd(double lat, double lon, const char *name, uint8_t flags);
void fieldWaypointName(const fieldWaypoint_t *wp, char *buf); // buf: FIELD_WAYPOINT_NAME_LEN + 1

// --- comms schedules (PACE > '#', quickkey "Sked") ----------------------------------------
#define FIELD_SKEDS_OFFSET         0x900U
#define FIELD_SKED_SLOTS           4

typedef struct
{
	uint32_t   magic;
	skedTime_t time;
	paceSlot_t channel; // time.tune == SKED_TUNE_CHANNEL
} fieldSked_t;

bool fieldSkedGet(int slot, fieldSked_t *sked); // false: empty
void fieldSkedSet(int slot, const fieldSked_t *sked); // NULL deletes

// Scratch RAM (CCM) for the field tool screens: only one of them is open at a time
#define FIELD_SCRATCH_SIZE         128U
extern uint32_t fieldToolsScratch[FIELD_SCRATCH_SIZE / sizeof(uint32_t)];

bool fieldCoverageAdd(const coveragePoint_t *point); // false: memory full
bool fieldCoverageGet(uint32_t index, coveragePoint_t *point);
void fieldCoverageClear(void);

#endif

#endif
