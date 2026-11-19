/*
 * Fork: field operations data in the SPI flash (SP7TEAM firmware, Marcin SP7TQ).
 *
 * 64 KB at the end of the 16 MB flash (taken from the end of the GPS NMEA log):
 *   sector 0     : PACE communication plan (Primary / Alternate / Contingency / Emergency)
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

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
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

// Scratch RAM (CCM) for the field tool screens: only one of them is open at a time
#define FIELD_SCRATCH_SIZE         128U
extern uint32_t fieldToolsScratch[FIELD_SCRATCH_SIZE / sizeof(uint32_t)];

bool fieldCoverageAdd(const coveragePoint_t *point); // false: memory full
bool fieldCoverageGet(uint32_t index, coveragePoint_t *point);
void fieldCoverageClear(void);

#endif

#endif
