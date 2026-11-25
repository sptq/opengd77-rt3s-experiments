/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "interfaces/gps.h"
#include "usb/usb_com.h"

#if defined(HAS_GPS)

static void updateScreen(bool isFirstRun, bool forceRedraw);
static void handleEvent(uiEvent_t *ev);

// Fork: "Waypoint" page. GREEN marks the current position (kept in RAM until
// power off); the page then shows distance and bearing back to it, with an
// arrow that is relative to the direction of travel when moving (ahead = up)
// and north-up when standing still.
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
#define GPS_WAYPOINT_PAGE
static bool waypointIsSet = false;
static double waypointLat = 0.0;
static double waypointLon = 0.0;
static bool displayWaypoint(bool isFirstRun, bool forceRedraw);
static bool waypointMarkHere(void);

// Fork: shared with the APRS RX monitor (radar marker, "station -> waypoint")
bool gpsWaypointGet(double *lat, double *lon)
{
	*lat = waypointLat;
	*lon = waypointLon;
	return waypointIsSet;
}

void gpsWaypointSet(double lat, double lon)
{
	waypointLat = lat;
	waypointLon = lon;
	waypointIsSet = true;
}

// Keypad entry of the waypoint (e.g. a radiosonde landing prediction).
// '#' on the waypoint page starts it; decimal degrees, '-' for S / W.
#define WAYPOINT_EDIT_FIELD_LEN 11
static bool waypointEditActive = false;
static uint8_t waypointEditFieldIndex = 0; // 0: latitude, 1: longitude
static char waypointEditFields[2][WAYPOINT_EDIT_FIELD_LEN + 1];
#endif

// Fork: trip computer page (distance, moving time, avg/max speed, altitude
// gain/loss), accumulated in the background by functions/gpsTrip.c.
#include "functions/gpsTrip.h"
#include "user_interface/uiEmcom.h"
#if defined(GPS_TRIP_COMPUTER)
static bool displayTrip(bool isFirstRun, bool forceRedraw);
#endif

#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
#define CURRENT_PAGE_ON_MONOCHOME(p)  if (menuDataGlobal.currentItemIndex == (p))
#else
#define CURRENT_PAGE_ON_MONOCHOME(p)
#endif

#define POLAR_GRAPHICS_X_OFFSET     0

#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
#if defined(PLATFORM_RD5R)
#define POLAR_GRAPHICS_Y_OFFSET    31
#define RSSI_X_OFFSET               0
#define RSSI_BARS_Y_POS            60
#define MAIDENHEAD_HDOP_Y_POS      40
#define NUM_BARS                   14
#define DOT_RADIUS                  1
#define MAIDENHEAD_HDOP_MARGIN      0

#define VALUES_FONT_SIZE            FONT_SIZE_3
#define VALUES_FONT_SIZE_SMALL      FONT_SIZE_2
#define UNITS_FONT_SIZE             FONT_SIZE_1
#define UNITS_Y_OFFSET              0
#define VALUES_RD5R_V_GAP           FONT_SIZE_3_HEIGHT
#define VALUES_RD5R_V_OFFSET        4

#define VALUES_FONT_WIDTH           8
#define VALUES_FONT_HEIGHT          8

#define DIRECTION_REGION_V_OFFSET   0
#define DIRECTION_REGION_H_OFFSET   0
#define CARDINALS_REGION_HEIGHT    (FONT_SIZE_2_HEIGHT - 1)

#define DIRECTION_SATS_X_POS       11
#define DIRECTION_SATS_Y_POS       28

#define DIRECTION_CARDINAL_OFFSET   8
#define DIRECTION_COURSE_X_POS     75
#define DIRECTION_COURSE_Y_POS     25

#define DIRECTION_ALT_X_POS        81
#define DIRECTION_ALT_Y_POS        28

#define DIRECTION_HDOP_X_POS       42
#define DIRECTION_HDOP_Y_POS       28

#define DIRECTION_HEADING_X_POS    28
#define DIRECTION_HEADING_Y_POS    31
#define DIRECTION_HEADING_RADIUS   16
#define TOP_ARROW_OFFSET           (DIRECTION_HEADING_RADIUS - 6)
#define BASE_ARROW_OFFSET          (DIRECTION_HEADING_RADIUS >> 1)

#define DIRECTION_FIXTYPE_X_POS    25
#define DIRECTION_FIXTYPE_Y_POS    28

#define DIRECTION_SPEED_X_POS      65
#define DIRECTION_SPEED_Y_POS      40

#define MAX_RADIUS                 15

#define MAX_RSSI_Y_PIXELS          29

#else // PLATFORM_RD5R

#define POLAR_GRAPHICS_Y_OFFSET    40
#define RSSI_X_OFFSET               0
#define RSSI_BARS_Y_POS            54
#define MAIDENHEAD_HDOP_Y_POS      48
#define NUM_BARS                   14
#define DOT_RADIUS                  1
#define MAIDENHEAD_HDOP_MARGIN      0

#define VALUES_FONT_SIZE            FONT_SIZE_3
#define VALUES_FONT_SIZE_SMALL      FONT_SIZE_2
#define UNITS_FONT_SIZE             FONT_SIZE_1
#define UNITS_Y_OFFSET              6
#define VALUES_RD5R_V_GAP           0
#define VALUES_RD5R_V_OFFSET        0

#define VALUES_FONT_WIDTH           8
#define VALUES_FONT_HEIGHT         16

#define DIRECTION_REGION_V_OFFSET   2
#define DIRECTION_REGION_H_OFFSET   5
#define CARDINALS_REGION_HEIGHT    (FONT_SIZE_2_HEIGHT - 1)

#define DIRECTION_SATS_X_POS       11
#define DIRECTION_SATS_Y_POS       34

#define DIRECTION_CARDINAL_OFFSET   8
#define DIRECTION_COURSE_X_POS     75
#define DIRECTION_COURSE_Y_POS     29

#define DIRECTION_ALT_X_POS        81
#define DIRECTION_ALT_Y_POS        34

#define DIRECTION_HDOP_X_POS       42
#define DIRECTION_HDOP_Y_POS       34

#define DIRECTION_HEADING_X_POS    28
#define DIRECTION_HEADING_Y_POS    39
#define DIRECTION_HEADING_RADIUS   24
#define TOP_ARROW_OFFSET           (DIRECTION_HEADING_RADIUS - 7)
#define BASE_ARROW_OFFSET         ((DIRECTION_HEADING_RADIUS >> 1) + 1)

#define DIRECTION_FIXTYPE_X_POS    25
#define DIRECTION_FIXTYPE_Y_POS    36

#define DIRECTION_SPEED_X_POS      65
#define DIRECTION_SPEED_Y_POS      47

#define MAX_RADIUS                 22

#define MAX_RSSI_Y_PIXELS          25

#endif // PLATFORM_RD5R

#else

#define POLAR_GRAPHICS_Y_OFFSET    72
#define RSSI_X_OFFSET               2
#define RSSI_BARS_Y_POS           120
#define MAIDENHEAD_HDOP_Y_POS      32
#define NUM_BARS                   17
#define DOT_RADIUS                  2
#define MAIDENHEAD_HDOP_MARGIN      8

#define VALUES_FONT_SIZE            FONT_SIZE_4
#define VALUES_FONT_SIZE_SMALL      FONT_SIZE_3
#define UNITS_FONT_SIZE             FONT_SIZE_2
#define UNITS_Y_OFFSET             18
#define VALUES_RD5R_V_GAP           0
#define VALUES_RD5R_V_OFFSET        0

#define VALUES_FONT_WIDTH          16
#define VALUES_FONT_HEIGHT         32

#define DIRECTION_REGION_V_OFFSET   6
#define DIRECTION_REGION_H_OFFSET  13
#define CARDINALS_REGION_HEIGHT     FONT_SIZE_3_HEIGHT

#define DIRECTION_SATS_X_POS      122
#define DIRECTION_SATS_Y_POS       30

#define DIRECTION_CARDINAL_OFFSET  10
#define DIRECTION_COURSE_X_POS     56
#define DIRECTION_COURSE_Y_POS     31

#define DIRECTION_ALT_X_POS        12
#define DIRECTION_ALT_Y_POS        71

#define DIRECTION_HDOP_X_POS      111
#define DIRECTION_HDOP_Y_POS       71

#define DIRECTION_HEADING_X_POS    27
#define DIRECTION_HEADING_Y_POS    39
#define DIRECTION_HEADING_RADIUS   25
#define TOP_ARROW_OFFSET           (DIRECTION_HEADING_RADIUS - 8)
#define BASE_ARROW_OFFSET          (DIRECTION_HEADING_RADIUS >> 1)

#define DIRECTION_FIXTYPE_X_POS    25
#define DIRECTION_FIXTYPE_Y_POS    36

#define DIRECTION_SPEED_X_POS      40
#define DIRECTION_SPEED_Y_POS      98

#define MAX_RADIUS                 44

#define MAX_RSSI_Y_PIXELS          59

#endif

typedef enum
{
#if defined(HAS_EMCOM_COLOUR_UI)
	PAGE_STATUS = 0, // Fork: EMCOM style only (overview); the classic style starts at PAGE_COORDS
	PAGE_COORDS,
#else
	PAGE_COORDS = 0,
#endif
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
	PAGE_RSSI,
#else
	PAGE_RSSI = PAGE_COORDS,
#endif
	PAGE_POLAR,
	PAGE_DIRECTION, // MD9600: Sat/HDOP/Alt, MD-UV380 everything in that page
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
	PAGE_DIRECTION_2, // Heading/Course
#endif
#if defined(GPS_WAYPOINT_PAGE)
	PAGE_WAYPOINT, // Fork: navigate back to a marked position
#endif
#if defined(GPS_TRIP_COMPUTER)
	PAGE_TRIP, // Fork: trip computer
#endif
	PAGES_MAX
} Pages_t;

typedef enum
{
	FIELD_SATS_CLEAR   = (1 << 0),
	FIELD_SATS_ZERO    = (1 << 1),
	FIELD_COURSE_CLEAR = (1 << 2),
	FIELD_COURSE_ZERO  = (1 << 3),
	FIELD_ALT_CLEAR    = (1 << 4),
	FIELD_ALT_ZERO     = (1 << 5),
	FIELD_HDOP_CLEAR   = (1 << 6),
	FIELD_HDOP_ZERO    = (1 << 7),
	FIELD_SPEED_CLEAR  = (1 << 8),
	FIELD_SPEED_ZERO   = (1 << 9),
	FIELD_BACKGROUND   = (1 << 10),
	FIELD_ALL_CLEAR    = (FIELD_SATS_CLEAR | FIELD_COURSE_CLEAR | FIELD_ALT_CLEAR | FIELD_HDOP_CLEAR | FIELD_SPEED_CLEAR),
	FIELD_ALL_ZERO     = (FIELD_SATS_ZERO | FIELD_COURSE_ZERO | FIELD_ALT_ZERO | FIELD_HDOP_ZERO | FIELD_SPEED_ZERO)
} DirectionFieldsFlags_t;

typedef enum
{
	FIXTYPE_NONE  = (1 << 1),
	FIXTYPE_1D    = (1 << 2),
	FIXTYPE_2D    = (1 << 3),
	FIXTYPE_3D    = (1 << 4),
} DirectionFixtype_t;

static menuStatus_t menuGPSExitCode = MENU_STATUS_SUCCESS;
static uint8_t prevGPSState = 0xff;
static uint32_t updateTick = 0;
static uint16_t prevSatsInView = 0xFFFF;
static char directions[16][4];

#if defined(HAS_EMCOM_COLOUR_UI)
static void emDrawPage(void);
static bool emIsEmcomPage(void);
#endif

// First page of the current style (the EMCOM style has the status page first)
static int gpsFirstPage(void)
{
#if defined(HAS_EMCOM_COLOUR_UI)
	if (emcomStyleEnabled() == false)
	{
		return PAGE_COORDS;
	}
#endif
	return 0;
}

menuStatus_t menuGPS(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = PAGES_MAX;
		if (menuDataGlobal.currentItemIndex < gpsFirstPage())
		{
			menuDataGlobal.currentItemIndex = gpsFirstPage();
		}
#if defined(GPS_WAYPOINT_PAGE)
		waypointEditActive = false;
#endif

		// Prepare directions from cardinals
		char N = currentLanguageGetSymbol(SYMBOLS_NORTH);
		char S = currentLanguageGetSymbol(SYMBOLS_SOUTH);
		char E = currentLanguageGetSymbol(SYMBOLS_EAST);
		char W = currentLanguageGetSymbol(SYMBOLS_WEST);

		sprintf(directions[0], "%c", N);
		sprintf(directions[1], "%c%c%c", N, N, E);
		sprintf(directions[2], "%c%c", N, E);
		sprintf(directions[3], "%c%c%c", E, N, E);
		sprintf(directions[4], "%c", E);
		sprintf(directions[5], "%c%c%c", E, S, E);
		sprintf(directions[6], "%c%c", S, E);
		sprintf(directions[7], "%c%c%c", S, S, E);
		sprintf(directions[8], "%c", S);
		sprintf(directions[9], "%c%c%c", S, S, W);
		sprintf(directions[10], "%c%c", S, W);
		sprintf(directions[11], "%c%c%c", W, S, W);
		sprintf(directions[12], "%c", W);
		sprintf(directions[13], "%c%c%c", W, N, W);
		sprintf(directions[14], "%c%c", N, W);
		sprintf(directions[15], "%c%c%c", N, N, W);

		prevGPSState = 0xff;

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->gps);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateScreen(true, false);

		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuGPSExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
		else
		{
			updateTick++;
			// Limits updateScreen() calling frequency rate to every second, if a new GPS info is available, GPS_STATUS_TIME_UPDATED excepted.
			// The functions called by updateScreen() are also filtering and limiting the screen redrawing, depending on the displayed page.
			if ((updateTick > 1000) &&
					((gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED | GPS_STATUS_POSITION_UPDATED |
							GPS_STATUS_HDOP_UPDATED | GPS_STATUS_COURSE_UPDATED | GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HEIGHT_UPDATED |
							GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED))
#if defined(HAS_EMCOM_COLOUR_UI)
							// Fork: the EMCOM status page has a clock and the search timer, redrawn every second
							|| (emcomStyleEnabled() && (menuDataGlobal.currentItemIndex == PAGE_STATUS) &&
									(SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF))
#endif
					))
			{
				updateTick = 0;
				updateScreen(false, false);
			}
		}
	}
	return menuGPSExitCode;
}

static int displayBars(gpsSatellitesData_t *satellitesArray, uint16_t satCount, bool numbersInBold, int dispPosition, float spacing, themeItem_t fgColour, themeItem_t bgColour)
{
#if ! defined(HAS_COLOURS)
#if ! defined(PLATFORM_RD5R)
	uint8_t numberFont = (numbersInBold ? FONT_SIZE_1_BOLD : FONT_SIZE_1);
#endif
#else
	uint8_t numberFont = FONT_SIZE_1;
#endif

	for (uint16_t i = 0; i < satCount; i++)
	{
		if (satellitesArray[i].RSSI > 0)
		{
			int xPos = RSSI_X_OFFSET + 2 + dispPosition * spacing;

			if (xPos > (DISPLAY_SIZE_X - spacing + 2))
			{
				break;
			}

			// Scale down (max @ 50dB), clipping higher
			int8_t rssi = SAFE_MIN(MAX_RSSI_Y_PIXELS, ((int8_t)((MAX_RSSI_Y_PIXELS * 0.020) * satellitesArray[i].RSSI)));
#if ! defined(PLATFORM_RD5R)
			char buf[3];
			uint16_t sNumber = (satellitesArray[i].Number % 100); // limits to 2 last digits

			displayThemeApply(THEME_ITEM_FG_GPS_NUMBER, THEME_ITEM_BG);
			sprintf(buf, "%d", (sNumber % 10));
			displayPrintAt(xPos + 1, RSSI_BARS_Y_POS, buf, numberFont);

			sprintf(buf, "%d", (sNumber / 10));
			displayPrintAt(xPos + 1, RSSI_BARS_Y_POS - 8, buf, numberFont);
#endif

			displayThemeApply(fgColour, bgColour);
			displayFillRect(xPos, RSSI_BARS_Y_POS - 12  - 2 - rssi, spacing - 2, rssi, false);
			displayThemeResetToDefault();

			dispPosition++;

			if (dispPosition > (NUM_BARS - 1))
			{
				return dispPosition;
			}
		}
	}

	return dispPosition;
}

static uint16_t getTotalNumberOfUsableSats(void)
{
	uint16_t n = 0;

	for (uint16_t i = 0; i < gpsData.SatsInViewGP; i++)
	{
		n += ((gpsData.GPSatellites[i].RSSI > 0) ? 1 : 0);
	}

	for (uint16_t i = 0; i < gpsData.SatsInViewBD; i++)
	{
		n += ((gpsData.BDSatellites[i].RSSI > 0) ? 1 : 0);
	}

	return n;
}

static void displayDirectionFixType(DirectionFixtype_t type)
{
	uint16_t fgColour, bgColour;

	displayGetForegroundAndBackgroundColours(&fgColour, &bgColour);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

	displayFillCircle(DIRECTION_HEADING_X_POS, DIRECTION_HEADING_Y_POS, (DIRECTION_HEADING_RADIUS >> 2) + 1, ((type & FIXTYPE_NONE) != 0));

	if (type & (FIXTYPE_1D | FIXTYPE_2D | FIXTYPE_3D))
	{
		displayDrawCircle(DIRECTION_HEADING_X_POS, DIRECTION_HEADING_Y_POS, (DIRECTION_HEADING_RADIUS >> 2) + 1, true);
	}

	if (type & (FIXTYPE_2D | FIXTYPE_3D))
	{
		displayThemeResetToDefault();
		displayPrintAt(DIRECTION_FIXTYPE_X_POS, DIRECTION_FIXTYPE_Y_POS, ((type & FIXTYPE_2D) ? "2" : "3"), FONT_SIZE_2);
	}

	displaySetForegroundAndBackgroundColours(fgColour, bgColour);
}

static void displayDirectionInfoBackground(DirectionFieldsFlags_t flags)
{
	//
	// Full background redrawing
	//
	if (flags & FIELD_BACKGROUND)
	{
		// Clear background
		displayFillRect(0, 16, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - 16), true);

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

		CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
		{
			// Sats in view
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			displayPrintAt(DIRECTION_SATS_X_POS - 3, (DIRECTION_SATS_Y_POS - 7), currentLanguage->satellite_short, FONT_SIZE_1);
			displayDrawFastHLine((DIRECTION_SATS_X_POS - 7), (DIRECTION_SATS_Y_POS - 9), 5, true);
			displayDrawFastVLine((DIRECTION_SATS_X_POS - 7), (DIRECTION_SATS_Y_POS - 8), FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6, true);
			displayDrawFastHLine((DIRECTION_SATS_X_POS - 7), ((DIRECTION_SATS_Y_POS - 8) + FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6), ((2 * 8) + 7), true);
			displayDrawFastVLine(((DIRECTION_SATS_X_POS - 7) + ((2 * 8) + 7)), ((DIRECTION_SATS_Y_POS - 4) + (FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP - 1)), 4, true);
#else
			displayPrintAt(DIRECTION_SATS_X_POS, (DIRECTION_SATS_Y_POS - 3), currentLanguage->satellite_short, FONT_SIZE_2);
			displayDrawFastHLine((DIRECTION_SATS_X_POS - 6), (DIRECTION_SATS_Y_POS - 5), 5, true);
			displayDrawFastVLine((DIRECTION_SATS_X_POS - 6), (DIRECTION_SATS_Y_POS - 4), FONT_SIZE_4_HEIGHT, true);
			displayDrawFastHLine((DIRECTION_SATS_X_POS - 6), ((DIRECTION_SATS_Y_POS - 4) + FONT_SIZE_4_HEIGHT), ((2 * 16) + 7), true);
			displayDrawFastVLine(((DIRECTION_SATS_X_POS - 6) + ((2 * 16) + 7)), ((DIRECTION_SATS_Y_POS - 4) + (FONT_SIZE_4_HEIGHT - 4)), 5, true);
#endif

			// Altitude
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			displayPrintAt(DIRECTION_ALT_X_POS - 3, (DIRECTION_ALT_Y_POS - 7), currentLanguage->altitude, FONT_SIZE_1);
			displayPrintAt((DIRECTION_ALT_X_POS + (4 * 8) + 2), (DIRECTION_ALT_Y_POS + 6), "m", FONT_SIZE_1);
			displayDrawFastHLine((DIRECTION_ALT_X_POS - 7), (DIRECTION_ALT_Y_POS - 9), 5, true);
			displayDrawFastVLine((DIRECTION_ALT_X_POS - 7), (DIRECTION_ALT_Y_POS - 8), FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6, true);
			displayDrawFastHLine((DIRECTION_ALT_X_POS - 7), ((DIRECTION_ALT_Y_POS - 8) + FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6), ((4 * 8) + 7 + 8), true);
			displayDrawFastVLine(((DIRECTION_ALT_X_POS - 7) + ((4 * 8) + 7) + 8), ((DIRECTION_ALT_Y_POS - 4) + (FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP - 1)), 4, true);
#else
			displayPrintAt(DIRECTION_ALT_X_POS, (DIRECTION_ALT_Y_POS - 3), currentLanguage->altitude, FONT_SIZE_2);
			displayPrintAt((DIRECTION_ALT_X_POS + (4 * 16) + 2), (DIRECTION_ALT_Y_POS + 18), "m", FONT_SIZE_2);
			displayDrawFastHLine((DIRECTION_ALT_X_POS - 6), (DIRECTION_ALT_Y_POS - 5), 5, true);
			displayDrawFastVLine((DIRECTION_ALT_X_POS - 6), (DIRECTION_ALT_Y_POS - 4), FONT_SIZE_4_HEIGHT, true);
			displayDrawFastHLine((DIRECTION_ALT_X_POS - 6), ((DIRECTION_ALT_Y_POS - 4) + FONT_SIZE_4_HEIGHT), ((4 * 16) + 7 + 10), true);
			displayDrawFastVLine(((DIRECTION_ALT_X_POS - 6) + ((4 * 16) + 7) + 10), ((DIRECTION_ALT_Y_POS - 4) + (FONT_SIZE_4_HEIGHT - 4)), 5, true);
#endif

			// HDOP
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			displayPrintAt(DIRECTION_HDOP_X_POS - 3, (DIRECTION_HDOP_Y_POS - 7), "HDOP", FONT_SIZE_1);
			displayPrintAt((DIRECTION_HDOP_X_POS + (2 * 8) + 2), (DIRECTION_HDOP_Y_POS + 6), "m", FONT_SIZE_1);
			displayDrawFastHLine((DIRECTION_HDOP_X_POS - 7), (DIRECTION_HDOP_Y_POS - 9), 5, true);
			displayDrawFastVLine((DIRECTION_HDOP_X_POS - 7), (DIRECTION_HDOP_Y_POS - 8), FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6, true);
			displayDrawFastHLine((DIRECTION_HDOP_X_POS - 7), ((DIRECTION_HDOP_Y_POS - 8) + FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP + 6), ((2 * 8) + 7 + 8), true);
			displayDrawFastVLine(((DIRECTION_HDOP_X_POS - 7) + ((2 * 8) + 7) + 8), ((DIRECTION_HDOP_Y_POS - 4) + (FONT_SIZE_3_HEIGHT + VALUES_RD5R_V_GAP - 1)), 4, true);
#else
			displayPrintAt(DIRECTION_HDOP_X_POS, (DIRECTION_HDOP_Y_POS - 3), "HDOP", FONT_SIZE_2);
			displayPrintAt((DIRECTION_HDOP_X_POS + (2 * 16) + 2), (DIRECTION_HDOP_Y_POS + 18), "m", FONT_SIZE_2);
			displayDrawFastHLine((DIRECTION_HDOP_X_POS - 6), (DIRECTION_HDOP_Y_POS - 5), 5, true);
			displayDrawFastVLine((DIRECTION_HDOP_X_POS - 6), (DIRECTION_HDOP_Y_POS - 4), FONT_SIZE_4_HEIGHT, true);
			displayDrawFastHLine((DIRECTION_HDOP_X_POS - 6), ((DIRECTION_HDOP_Y_POS - 4) + FONT_SIZE_4_HEIGHT), ((2 * 16) + 7 + 10), true);
			displayDrawFastVLine(((DIRECTION_HDOP_X_POS - 6) + ((2 * 16) + 7) + 10), ((DIRECTION_HDOP_Y_POS - 4) + (FONT_SIZE_4_HEIGHT - 4)), 5, true);
#endif

		}

		CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION_2)
		{
			// Course
			char deg[2] = { 176, 0 };
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			displayPrintAt((DIRECTION_COURSE_X_POS + (3 * 8)), (DIRECTION_COURSE_Y_POS + 1), deg, FONT_SIZE_1);
			displayFillRect(((DIRECTION_COURSE_X_POS + 12) - 12 - 5), (DIRECTION_COURSE_Y_POS - 6), 4, 2, false);
			displayFillRect(((DIRECTION_COURSE_X_POS + 12) + 12 + 1), (DIRECTION_COURSE_Y_POS - 6), 4, 2, false);
			displayDrawFastVLine((DIRECTION_COURSE_X_POS - 7), (((DIRECTION_COURSE_Y_POS - 3) + FONT_SIZE_3_HEIGHT + (VALUES_RD5R_V_GAP >> 1) + 2) - 4), 4, true);
			displayDrawFastHLine((DIRECTION_COURSE_X_POS - 6), ((DIRECTION_COURSE_Y_POS - 3) + FONT_SIZE_3_HEIGHT + (VALUES_RD5R_V_GAP >> 1) + 1), ((3 * 8) + 7 + 7), true);
			displayDrawFastVLine(((DIRECTION_COURSE_X_POS - 1) + ((3 * 8) + 7 + 2)), (DIRECTION_COURSE_Y_POS - 8), (FONT_SIZE_3_HEIGHT + (VALUES_RD5R_V_GAP >> 1) + 7), true);
			displayDrawFastHLine(((DIRECTION_COURSE_X_POS - 1) + ((3 * 8) + 7 + 2) - 4), (DIRECTION_COURSE_Y_POS - 9), 5, true);
#else
			displayPrintAt((DIRECTION_COURSE_X_POS + (3 * 16) + 1), (DIRECTION_COURSE_Y_POS + 5), deg, FONT_SIZE_2);
			displayFillRect(((DIRECTION_COURSE_X_POS + 24) - 12 - 8), (DIRECTION_COURSE_Y_POS - 4), 7, 2, false);
			displayFillRect(((DIRECTION_COURSE_X_POS + 24) + 12 + 1), (DIRECTION_COURSE_Y_POS - 4), 7, 2, false);
			displayDrawFastVLine((DIRECTION_COURSE_X_POS - 2),  (((DIRECTION_COURSE_Y_POS - 5) + FONT_SIZE_4_HEIGHT) - 4), 5, true);
			displayDrawFastHLine((DIRECTION_COURSE_X_POS - 1), ((DIRECTION_COURSE_Y_POS - 5) + FONT_SIZE_4_HEIGHT), ((3 * 16) + 7 + 2), true);
			displayDrawFastVLine(((DIRECTION_COURSE_X_POS - 1) + ((3 * 16) + 7 + 2)), (DIRECTION_COURSE_Y_POS - 5), (FONT_SIZE_4_HEIGHT + 1), true);
			displayDrawFastHLine(((DIRECTION_COURSE_X_POS - 1) + ((3 * 16) + 7 + 2) - 4), (DIRECTION_COURSE_Y_POS - 6), 5, true);
#endif
			// Heading
			{

				for (int16_t i = 0; i < 360; i += 15)
				{
					int16_t x1 = (int16_t)(DIRECTION_HEADING_X_POS + (DIRECTION_HEADING_RADIUS) * cos((double)(i * DEG_TO_RAD)));
					int16_t y1 = (int16_t)(DIRECTION_HEADING_Y_POS + (DIRECTION_HEADING_RADIUS) * sin((double)(i * DEG_TO_RAD)));

					int16_t x2 = (int16_t)(DIRECTION_HEADING_X_POS + (DIRECTION_HEADING_RADIUS - ((i % 45) ? 2 : 4)) * cos((double)(i * DEG_TO_RAD)));
					int16_t y2 = (int16_t)(DIRECTION_HEADING_Y_POS + (DIRECTION_HEADING_RADIUS - ((i % 45) ? 2 : 4)) * sin((double)(i * DEG_TO_RAD)));

					if (i == 270)
					{
						displayThemeApply(THEME_ITEM_FG_GPS_COLOUR, THEME_ITEM_BG);
						displayDrawLine((x1 - 1), y1, (x2 - 1), y2, true);
					}

					displayDrawLine(x1, y1, x2, y2, true);

					if (i == 270)
					{
						displayDrawLine((x1 + 1), y1, (x2 + 1), y2, true);
						displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
					}
				}

				displayDrawCircle(DIRECTION_HEADING_X_POS, DIRECTION_HEADING_Y_POS, DIRECTION_HEADING_RADIUS, true);
				displayDirectionFixType(FIXTYPE_NONE);
			}


			// Speed
			displayPrintAt((DIRECTION_SPEED_X_POS + (3 * VALUES_FONT_WIDTH) + 2), (DIRECTION_SPEED_Y_POS + UNITS_Y_OFFSET), "km/h", UNITS_FONT_SIZE);
		}

		displayThemeResetToDefault();
	}

	//
	// Region clearing
	//
	CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
	{
		if (flags & (FIELD_SATS_CLEAR | FIELD_SATS_ZERO))
		{
			displayFillRect(DIRECTION_SATS_X_POS, (DIRECTION_SATS_Y_POS + VALUES_RD5R_V_OFFSET + DIRECTION_REGION_V_OFFSET), (2 * VALUES_FONT_WIDTH), (VALUES_FONT_HEIGHT - DIRECTION_REGION_H_OFFSET), true);
		}

		if (flags & (FIELD_ALT_CLEAR | FIELD_ALT_ZERO))
		{
			displayFillRect(DIRECTION_ALT_X_POS, (DIRECTION_ALT_Y_POS + VALUES_RD5R_V_OFFSET + DIRECTION_REGION_V_OFFSET), (4 * VALUES_FONT_WIDTH), (VALUES_FONT_HEIGHT - DIRECTION_REGION_H_OFFSET), true);
		}

		if (flags & (FIELD_HDOP_CLEAR | FIELD_HDOP_ZERO))
		{
			displayFillRect(DIRECTION_HDOP_X_POS, (DIRECTION_HDOP_Y_POS + VALUES_RD5R_V_OFFSET + DIRECTION_REGION_V_OFFSET), (2 * VALUES_FONT_WIDTH), (VALUES_FONT_HEIGHT - DIRECTION_REGION_H_OFFSET), true);
		}
	}

	CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION_2)
	{
		if (flags & (FIELD_COURSE_CLEAR | FIELD_COURSE_ZERO))
		{
			displayFillRect(((DIRECTION_COURSE_X_POS + ((VALUES_FONT_WIDTH * 3) >> 1)) - 12), (DIRECTION_COURSE_Y_POS - DIRECTION_CARDINAL_OFFSET), (3 * 8), CARDINALS_REGION_HEIGHT, true);
			displayFillRect(DIRECTION_COURSE_X_POS, (DIRECTION_COURSE_Y_POS + DIRECTION_REGION_V_OFFSET), (3 * VALUES_FONT_WIDTH), (VALUES_FONT_HEIGHT - DIRECTION_REGION_H_OFFSET), true);
			displayFillCircle(DIRECTION_HEADING_X_POS, DIRECTION_HEADING_Y_POS, (DIRECTION_HEADING_RADIUS - 6), false);
			displayDirectionFixType(FIXTYPE_NONE);
		}

		if (flags & (FIELD_SPEED_CLEAR | FIELD_SPEED_ZERO))
		{
			displayFillRect(DIRECTION_SPEED_X_POS, (DIRECTION_SPEED_Y_POS + DIRECTION_REGION_V_OFFSET), (3 * VALUES_FONT_WIDTH), (VALUES_FONT_HEIGHT - DIRECTION_REGION_H_OFFSET), true);
		}
	}

	//
	// Zeroing values
	//
	CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
	{
		if (flags & FIELD_SATS_ZERO)
		{
			displayPrintAt(DIRECTION_SATS_X_POS, DIRECTION_SATS_Y_POS + VALUES_RD5R_V_OFFSET, "--", VALUES_FONT_SIZE);
		}

		if (flags & FIELD_ALT_ZERO)
		{
			displayPrintAt(DIRECTION_ALT_X_POS, DIRECTION_ALT_Y_POS + VALUES_RD5R_V_OFFSET, "----", VALUES_FONT_SIZE);
		}

		if (flags & FIELD_HDOP_ZERO)
		{
			displayPrintAt(DIRECTION_HDOP_X_POS, DIRECTION_HDOP_Y_POS + VALUES_RD5R_V_OFFSET, "--", VALUES_FONT_SIZE);
		}
	}

	CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION_2)
	{
		if (flags & FIELD_COURSE_ZERO)
		{
			displayPrintAt(((DIRECTION_COURSE_X_POS + ((VALUES_FONT_WIDTH * 3) >> 1)) - 12), (DIRECTION_COURSE_Y_POS - DIRECTION_CARDINAL_OFFSET), "---", VALUES_FONT_SIZE_SMALL);
			displayPrintAt(DIRECTION_COURSE_X_POS, DIRECTION_COURSE_Y_POS, "---", VALUES_FONT_SIZE);
		}

		if (flags & FIELD_SPEED_ZERO)
		{
			displayPrintAt(DIRECTION_SPEED_X_POS, DIRECTION_SPEED_Y_POS, "---", VALUES_FONT_SIZE);
		}
	}
}

static void displayDirectionCourseZeroing(void)
{
	DirectionFixtype_t ft = FIXTYPE_1D;

	if (gpsData.Status & (GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX))
	{
		ft &= ~FIXTYPE_1D;
		ft |= ((gpsData.Status & GPS_STATUS_2D_FIX) ? FIXTYPE_2D : FIXTYPE_3D);
	}

	displayDirectionInfoBackground(FIELD_COURSE_ZERO);
	displayDirectionFixType(ft);
}

static bool displayDirectionInfo(bool isFirstRun, bool forceRedraw)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	if (gpsData.Status & GPS_STATUS_HAS_FIX)
	{
		uint16_t inViewAndUsableSats = getTotalNumberOfUsableSats();

		if (isFirstRun || forceRedraw || (prevSatsInView != inViewAndUsableSats) ||
				(gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_COURSE_UPDATED |
						GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HDOP_UPDATED | GPS_STATUS_FIXTYPE_UPDATED | GPS_STATUS_HEIGHT_UPDATED)))
		{
			// Draw the background
			if (isFirstRun || forceRedraw)
			{
				displayDirectionInfoBackground(FIELD_BACKGROUND);
			}

			// Update, only clear screen regions
			if (((isFirstRun || forceRedraw) == false) &&
					((gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_COURSE_UPDATED |
							GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HDOP_UPDATED | GPS_STATUS_FIXTYPE_UPDATED | GPS_STATUS_HEIGHT_UPDATED))))
			{
				DirectionFieldsFlags_t f = 0;

				if (gpsData.Status & GPS_STATUS_COURSE_UPDATED)
				{
					f |= FIELD_COURSE_CLEAR;
				}

				if (gpsData.Status & GPS_STATUS_SPEED_UPDATED)
				{
					f |= FIELD_SPEED_CLEAR;
				}

				if (gpsData.Status & GPS_STATUS_HDOP_UPDATED)
				{
					f |= FIELD_HDOP_CLEAR;
				}

				if (gpsData.Status & GPS_STATUS_HEIGHT_UPDATED)
				{
					f |= FIELD_ALT_CLEAR;
				}

				displayDirectionInfoBackground(f);
			}

			if (isFirstRun || forceRedraw || (prevSatsInView != inViewAndUsableSats))
			{
				CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
				{
					displayDirectionInfoBackground(FIELD_SATS_CLEAR);
					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2u", inViewAndUsableSats);
					displayPrintAt(DIRECTION_SATS_X_POS, DIRECTION_SATS_Y_POS + VALUES_RD5R_V_OFFSET, buffer, VALUES_FONT_SIZE);
				}
			}

			CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION_2)
			{
				if (isFirstRun || forceRedraw || (gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED | GPS_STATUS_COURSE_UPDATED)))
				{
					DirectionFixtype_t ft = FIXTYPE_1D;

					if (gpsData.Status & (GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX))
					{
						ft &= ~FIXTYPE_1D;
						ft |= ((gpsData.Status & GPS_STATUS_2D_FIX) ? FIXTYPE_2D : FIXTYPE_3D);
					}

					displayDirectionFixType(ft);
				}

				if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_COURSE_UPDATED))
				{
					int dir = (int)(((gpsData.CourseInHundredthDeg * 1E-2) + 11.25f) / 22.5f);

					if (gpsData.SpeedInHundredthKn > GPS_SPEED_THRESHOLD_MIN)
					{
						int16_t x1, x2, x3, y1, y2, y3;

						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s", directions[dir % 16]);
						displayPrintAt(((DIRECTION_COURSE_X_POS + ((VALUES_FONT_WIDTH * 3) >> 1)) - ((strlen(buffer) * 8) >> 1)), (DIRECTION_COURSE_Y_POS - DIRECTION_CARDINAL_OFFSET), buffer, VALUES_FONT_SIZE_SMALL);

						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%03u", (uint16_t)(gpsData.CourseInHundredthDeg * 1E-2));
						displayPrintAt(DIRECTION_COURSE_X_POS, DIRECTION_COURSE_Y_POS, buffer, VALUES_FONT_SIZE);

						displayThemeApply(THEME_ITEM_FG_GPS_COLOUR, THEME_ITEM_BG);

						// Needle
						float needle = ((gpsData.CourseInHundredthDeg * 1E-2) - 90.0);

						if (needle > 360.0)
						{
							needle -= 360.0;
						}

						needle *= DEG_TO_RAD;

						x1 = (int16_t)(DIRECTION_HEADING_X_POS + 1 + TOP_ARROW_OFFSET * cos(needle));
						y1 = (int16_t)(DIRECTION_HEADING_Y_POS + 1 + TOP_ARROW_OFFSET * sin(needle));

						x2 = (int16_t)(DIRECTION_HEADING_X_POS + 1 + BASE_ARROW_OFFSET * cos(needle + (30 * DEG_TO_RAD)));
						y2 = (int16_t)(DIRECTION_HEADING_Y_POS + 1 + BASE_ARROW_OFFSET * sin(needle + (30 * DEG_TO_RAD)));

						x3 = (int16_t)(DIRECTION_HEADING_X_POS + 1 + BASE_ARROW_OFFSET * cos(needle - (30 * DEG_TO_RAD)));
						y3 = (int16_t)(DIRECTION_HEADING_Y_POS + 1 + BASE_ARROW_OFFSET * sin(needle - (30 * DEG_TO_RAD)));

						displayFillTriangle(x1, y1, x2, y2, x3, y3, true);
						displayThemeResetToDefault();
					}
					else
					{
						displayDirectionCourseZeroing();
					}
				}

				if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_SPEED_UPDATED))
				{
					uint16_t speed = (uint16_t)((gpsData.SpeedInHundredthKn * 1E-2) * KMPH_PER_KNOT);

					// Speed < 1km/h and Course is not not updated => display zero course
					if ((speed == 0) && ((isFirstRun || forceRedraw) == false) && ((gpsData.Status & GPS_STATUS_COURSE_UPDATED) == 0))
					{
						displayDirectionCourseZeroing();
					}

					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%03u", speed);
					displayPrintAt(DIRECTION_SPEED_X_POS, DIRECTION_SPEED_Y_POS, buffer, VALUES_FONT_SIZE);
				}
			}

			CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
			{
				if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_HDOP_UPDATED))
				{
					if (gpsData.AccuracyInCm < 100)
					{
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s", "<1");
					}
					else
					{
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2u", SAFE_MIN((gpsData.AccuracyInCm / 100), 99));
					}

					displayPrintAt(DIRECTION_HDOP_X_POS, DIRECTION_HDOP_Y_POS + VALUES_RD5R_V_OFFSET, buffer, VALUES_FONT_SIZE);
				}
			}

			if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_HEIGHT_UPDATED))
			{
				CURRENT_PAGE_ON_MONOCHOME(PAGE_DIRECTION)
				{
					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%4d", gpsData.HeightInM);
					displayPrintAt(DIRECTION_ALT_X_POS, DIRECTION_ALT_Y_POS + VALUES_RD5R_V_OFFSET, buffer, VALUES_FONT_SIZE);
				}
			}

			return true;
		}
	}
	else
	{
		//                               |--> fix got lost
		if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_FIX_UPDATED))
		{
			displayDirectionInfoBackground((FIELD_BACKGROUND | FIELD_ALL_ZERO));
			return true;
		}
	}

	return false;
}

static bool displayRSSI(bool isFirstRun, bool forceRedraw)
{
	float spacing = 9.0;
	int dispPosition = 0;

	if (isFirstRun || forceRedraw || (gpsData.Status & (GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED)))
	{
		// Update, only clear screen regions
		if (((isFirstRun || forceRedraw) == false) && (gpsData.Status & (GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED)))
		{
			// Bars.
			displayFillRect(0, (RSSI_BARS_Y_POS - 12 - 2 - MAX_RSSI_Y_PIXELS), DISPLAY_SIZE_X, MAX_RSSI_Y_PIXELS, true);
#if ! defined(PLATFORM_RD5R)
			// Numbers
			displayFillRect(0, (RSSI_BARS_Y_POS - 8), DISPLAY_SIZE_X, (8 * 2), true);
#endif
		}

		if (isFirstRun || forceRedraw)
		{
			for(int i = 0; i < NUM_BARS; i++)
			{
				int xPos = 2 + (i * spacing);

				displayFillRect(RSSI_X_OFFSET + xPos, RSSI_BARS_Y_POS - 12 - 2, spacing - 2, 2, false);
			}
		}

		if ((gpsData.SatsInViewGP + gpsData.SatsInViewBD) > 0)
		{
			dispPosition = displayBars(gpsData.GPSatellites, gpsData.SatsInViewGP, false, dispPosition, spacing, THEME_ITEM_FG_GPS_COLOUR, THEME_ITEM_BG);
			displayBars(gpsData.BDSatellites, gpsData.SatsInViewBD, true, dispPosition, spacing, THEME_ITEM_FG_BD_COLOUR, THEME_ITEM_BG);
		}

		return true;
	}

	return false;
}

static void displaySatellitesPolar(gpsSatellitesData_t *satellitesArray, bool circle)
{
	float az, elFactor, s, c;
	int x, y;

	for (int i = 0; ((i < GPS_STORAGE_MAX) && (satellitesArray[i].Number != 0)); i++)
	{
		if (satellitesArray[i].RSSI > 0)
		{
			az = (M_PI * satellitesArray[i].Az) / 180.0;

			if (satellitesArray[i].El <= 0)
			{
				elFactor = 0;// put in the middle of the screen if there is no real elevation data
			}
			else
			{
				elFactor = (1 - (fabs(satellitesArray[i].El) / 90)) * MAX_RADIUS;
			}

			s = sin(az);
			c = cos(az);
			x = POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2) + (s * elFactor);
			y = POLAR_GRAPHICS_Y_OFFSET - (c * elFactor);
#if ! defined(HAS_COLOURS)
			if (circle)
			{
				displayFillCircle(x, y, (DOT_RADIUS + 1), false);
				displayDrawCircle(x, y, (DOT_RADIUS + 1), true);
			}
			else
#endif
			{
				displayFillCircle(x, y, (DOT_RADIUS + 1), true);
			}
		}
	}
}

static bool displayPolar(bool isFirstRun, bool forceRedraw)
{
	if (isFirstRun || forceRedraw || (gpsData.Status & (GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED)))
	{
		displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);

		// Update, only clear screen region
		if (((isFirstRun || forceRedraw) == false) && (gpsData.Status & (GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED)))
		{
			displayFillCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), POLAR_GRAPHICS_Y_OFFSET, MAX_RADIUS, false);
		}

		displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), POLAR_GRAPHICS_Y_OFFSET, MAX_RADIUS, true);
		displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), POLAR_GRAPHICS_Y_OFFSET, (MAX_RADIUS / 3 ) * 2, true);
		displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), POLAR_GRAPHICS_Y_OFFSET, (MAX_RADIUS / 3 ), true);

#if defined(PLATFORM_RD5R)
		displayDrawFastVLine(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2),
				(POLAR_GRAPHICS_Y_OFFSET) - MAX_RADIUS, MAX_RADIUS * 2, true);
		displayDrawFastHLine((POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2)) - MAX_RADIUS,
				(POLAR_GRAPHICS_Y_OFFSET), MAX_RADIUS * 2, true);
#else
		displayDrawFastVLine(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2),
				(POLAR_GRAPHICS_Y_OFFSET) - (MAX_RADIUS + 4), (MAX_RADIUS + 4) * 2, true);
		displayDrawFastHLine((POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2)) - (MAX_RADIUS + 4),
				(POLAR_GRAPHICS_Y_OFFSET), (MAX_RADIUS + 4) * 2, true);
#endif

		if ((gpsData.SatsInViewGP + gpsData.SatsInViewBD) > 0)
		{
			displayThemeApply(THEME_ITEM_FG_GPS_COLOUR, THEME_ITEM_BG);
			displaySatellitesPolar(gpsData.GPSatellites, false);
			displayThemeApply(THEME_ITEM_FG_BD_COLOUR, THEME_ITEM_BG);
			displaySatellitesPolar(gpsData.BDSatellites, true);
		}

		displayThemeResetToDefault();

		return true;
	}

	return false;
}

static bool displayCoords(bool isFirstRun, bool forceRedraw)
{
	char buffer[LOCATION_TEXT_BUFFER_SIZE]; // larger screen is available on MD-UV380 and friends

	if (gpsData.Status & GPS_STATUS_HAS_FIX)
	{
		if (isFirstRun || forceRedraw || (gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_POSITION_UPDATED | GPS_STATUS_HDOP_UPDATED | GPS_STATUS_HEIGHT_UPDATED)))
		{
			const uint16_t coords_Y_positions[] = {
#if defined(PLATFORM_RD5R)
					16U, 26U
#else
					16U, 32U
#endif
			};
			char maidenheadBuf[7];
			char *p;

			// Update, only clear screen regions
			if (((isFirstRun || forceRedraw) == false) && (gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_POSITION_UPDATED | GPS_STATUS_HDOP_UPDATED | GPS_STATUS_HEIGHT_UPDATED)))
			{
				displayFillRect(0, (int16_t)coords_Y_positions[0], DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
				displayFillRect(0, (int16_t)coords_Y_positions[1], DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
#endif
				displayFillRect(0, MAIDENHEAD_HDOP_Y_POS, DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
			}

			buildLocationAndMaidenheadStrings(buffer, maidenheadBuf, true);

			p = strchr(buffer, ' ');

			*p = 0; // split locations Lat/Lon parts

#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			// Lat: Centered
			displayPrintAt(((DISPLAY_SIZE_X - (8 * 8)) >> 1), coords_Y_positions[0], buffer, FONT_SIZE_3);

			// Lon: Centered
			displayPrintAt(((DISPLAY_SIZE_X - (9 * 8)) >> 1), coords_Y_positions[1], (p + 1), FONT_SIZE_3);
#else
			// Lat: Left
			displayPrintAt(5, 16, buffer, FONT_SIZE_3);

			// Lon: Right
			displayPrintAt((DISPLAY_SIZE_X - ((9 * 8) + 5)), coords_Y_positions[0], (p + 1), FONT_SIZE_3);
#endif

			displayPrintAt(MAIDENHEAD_HDOP_MARGIN, MAIDENHEAD_HDOP_Y_POS, maidenheadBuf, FONT_SIZE_3);

			if (gpsData.AccuracyInCm < 100)
			{
				snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s", "+/-<1m");
			}
			else
			{
				snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "+/-%dm", (gpsData.AccuracyInCm / 100));
			}

			displayPrintAt((DISPLAY_SIZE_X - ((strlen(buffer) * 8) + MAIDENHEAD_HDOP_MARGIN)), MAIDENHEAD_HDOP_Y_POS, buffer, FONT_SIZE_3);

			return true;
		}
	}

	return false;
}

#if defined(HAS_EMCOM_COLOUR_UI)
//
// Fork: EMCOM style pages: status (overview), signal (one bar per satellite) and sky plot.
// They redraw their whole screen into the frame buffer (rendered once, no flicker). The classic
// drawing above is used by the classic style and by the other pages.
//
// The receiver does not report which satellites are used in the fix (GSA is not stored), so
// the pages tell satellites with a signal (SNR > 0) from satellites only in view.
//
#define EM_STRIP_Y        MENU_HEADER_HEIGHT
#define EM_STRIP_H        11
#define EM_KEYBAR_H       11
#define EM_KEYBAR_Y       (DISPLAY_SIZE_Y - EM_KEYBAR_H)
#define EM_TOP            (EM_STRIP_Y + EM_STRIP_H)

#define EM_SAT_BD         0x80U // satellite index flag: BeiDou array

// Signal page: bars area x EM_CH_X..158, 0 dB-Hz at y EM_CH_BASE, EM_CH_MAX_DB at EM_CH_TOP
#define EM_CH_X           14
#define EM_CH_RIGHT       (DISPLAY_SIZE_X - 2)
#define EM_CH_TOP         (EM_TOP + 10)
#define EM_CH_BASE        98
#define EM_CH_MAX_DB      50
#define EM_CH_MAX_BARS    ((EM_CH_RIGHT - EM_CH_X) / 6)  // 24 bars of 5 px
#define EM_CH_MAX_PITCH   14

// Sky plot
#define EM_SKY_CX         50
#define EM_SKY_R          44
#define EM_SKY_CY         ((EM_TOP + EM_KEYBAR_Y) / 2)
#define EM_SKY_RIGHT      98    // labels stay left of the right column
#define EM_SKY_COL_X      102
#define EM_SKY_MAX_SATS   40
#define EM_SKY_MAX_LABELS 18    // 2 ring elevations + 16 PRN numbers

typedef enum
{
	EM_SYM_SOLID = 0, // GPS bar, BeiDou on the sky plot
	EM_SYM_HATCH,     // BeiDou bar
	EM_SYM_DISC       // GPS on the sky plot
} emSymbol_t;

static void emText(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg, themeItem_t bg)
{
	displayThemeApply(fg, bg);
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static int16_t emTextWidth(const char *s, ucFont_t font)
{
	return (int16_t)(strlen(s) * (((font == FONT_SIZE_1) || (font == FONT_SIZE_1_BOLD)) ? 6 : 8));
}

static void emTextRight(int16_t xRight, int16_t y, const char *s, ucFont_t font, themeItem_t fg, themeItem_t bg)
{
	emText((int16_t)(xRight - emTextWidth(s, font)), y, s, font, fg, bg);
}

// Small label + value, e.g. "HDOP 0.9"; right aligned when xRight is true
static void emKeyValue(int16_t x, int16_t y, const char *label, const char *value, themeItem_t colour, bool alignRight)
{
	int16_t lw = (int16_t)(emTextWidth(label, FONT_SIZE_1) + 4);
	int16_t vw = emTextWidth(value, FONT_SIZE_1);

	if (alignRight)
	{
		x = (int16_t)(x - (lw + vw));
	}
	emText(x, y, label, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText((int16_t)(x + lw), y, value, FONT_SIZE_1, colour, THEME_ITEM_BG);
}

// Coloured field on the status strip, text in the background colour; returns its width
static int16_t emChip(int16_t x, int16_t y, const char *text, themeItem_t colour)
{
	int16_t w = (int16_t)(emTextWidth(text, FONT_SIZE_1) + 4);

	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillRect(x, y, w, 9, false);
	displayPrintCore((x + 2), y, text, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	return w;
}

// Big field (8x16 font), text in the background colour
static void emBigChip(int16_t x, int16_t y, const char *text, themeItem_t colour)
{
	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillRect(x, y, (emTextWidth(text, FONT_SIZE_3) + 6), 18, false);
	displayPrintCore((x + 3), (y + 1), text, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
}

static void emKeyBar(const char *keys)
{
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EM_KEYBAR_Y, DISPLAY_SIZE_X, EM_KEYBAR_H, false);
	emText(3, (EM_KEYBAR_Y + 2), keys, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
}

// "45s", "3:12", "1:02:03"
static void emDuration(char *buf, size_t size, uint32_t secs)
{
	if (secs >= 3600U)
	{
		snprintf(buf, size, "%u:%02u:%02u", (unsigned)MIN((secs / 3600U), 99U), (unsigned)((secs / 60U) % 60U), (unsigned)(secs % 60U));
	}
	else if (secs >= 60U)
	{
		snprintf(buf, size, "%u:%02u", (unsigned)(secs / 60U), (unsigned)(secs % 60U));
	}
	else
	{
		snprintf(buf, size, "%us", (unsigned)secs);
	}
}

// 7 x 7 legend / plot symbol, top left at (x, y)
static void emSymbol(int16_t x, int16_t y, emSymbol_t symbol, bool filled, themeItem_t colour, themeItem_t bg)
{
	displayThemeApply(colour, bg);

	switch (symbol)
	{
		case EM_SYM_DISC:
			if (filled)
			{
				displayFillCircle((x + 3), (y + 3), 3, true);
			}
			else
			{
				displayDrawCircle((x + 3), (y + 3), 3, true);
			}
			break;

		case EM_SYM_HATCH:
			displayDrawRect(x, y, 7, 7, true);
			displayDrawFastHLine(x, (y + 2), 7, true);
			displayDrawFastHLine(x, (y + 4), 7, true);
			break;

		default:
			if (filled)
			{
				displayFillRect(x, y, 7, 7, false);
			}
			else
			{
				displayDrawRect(x, y, 7, 7, true);
			}
			break;
	}
}

static uint8_t emSatsInView(bool beidou)
{
	return (uint8_t)MIN((beidou ? gpsData.SatsInViewBD : gpsData.SatsInViewGP), GPS_STORAGE_MAX);
}

static gpsSatellitesData_t *emSat(uint8_t code)
{
	return ((code & EM_SAT_BD) ? &gpsData.BDSatellites[code & 0x7FU] : &gpsData.GPSatellites[code]);
}

static uint8_t emSatsHeard(bool beidou)
{
	uint8_t n = 0;

	for (uint8_t i = 0; i < emSatsInView(beidou); i++)
	{
		n += ((beidou ? gpsData.BDSatellites[i].RSSI : gpsData.GPSatellites[i].RSSI) > 0) ? 1 : 0;
	}
	return n;
}

// The satellites in view, strongest first (no signal last), at most maxCount; returns the
// number stored, *total gets the number in view.
static uint8_t emSortedSats(uint8_t *codes, uint8_t maxCount, uint8_t *total)
{
	uint8_t n = 0;

	*total = 0;
	for (uint8_t c = 0; c < 2; c++)
	{
		uint8_t count = emSatsInView(c != 0);

		for (uint8_t i = 0; i < count; i++)
		{
			uint8_t code = (uint8_t)(i | ((c != 0) ? EM_SAT_BD : 0U));
			int8_t rssi = emSat(code)->RSSI;
			uint8_t pos = n;

			(*total)++;

			// insertion: after the ones as strong or stronger
			while ((pos > 0) && (emSat(codes[pos - 1])->RSSI < rssi))
			{
				pos--;
			}

			if (pos >= maxCount)
			{
				continue; // weaker than all the stored ones, list full
			}

			if (n < maxCount)
			{
				n++;
			}

			for (uint8_t k = (uint8_t)(n - 1); k > pos; k--)
			{
				codes[k] = codes[k - 1];
			}
			codes[pos] = code;
		}
	}

	return n;
}

static bool emGpsIsOn(void)
{
	return (SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF);
}

// Fix state word and colour
static const char *emFixState(themeItem_t *colour)
{
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) == GPS_NOT_DETECTED)
	{
		*colour = THEME_ITEM_FG_TX_COUNTER;
		return "NO GPS";
	}

	if (emGpsIsOn() == false)
	{
		*colour = THEME_ITEM_FG_DECORATION;
		return "GPS OFF";
	}

	if (gpsData.Status & GPS_STATUS_HAS_FIX)
	{
		if (gpsData.Status & GPS_STATUS_3D_FIX)
		{
			*colour = THEME_ITEM_FG_RSSI_BAR;
			return "3D FIX";
		}

		*colour = THEME_ITEM_FG_RSSI_BAR_S9P;
		return ((gpsData.Status & GPS_STATUS_2D_FIX) ? "2D FIX" : "FIX");
	}

	*colour = THEME_ITEM_FG_TX_COUNTER;
	return "NO FIX";
}

// HDOP value + word; returns its colour
static themeItem_t emHdop(char *value, size_t size, const char **word)
{
	uint16_t h = gpsData.AccuracyInCm; // HDOP x 100

	// (the HAS_* bits of gps.c are only set again when a value changes after a lost fix: not used)
	if (((gpsData.Status & GPS_STATUS_HAS_FIX) == 0) || (h == 0U))
	{
		snprintf(value, size, "--");
		*word = "";
		return THEME_ITEM_FG_DECORATION;
	}

	if (h < 1000U)
	{
		snprintf(value, size, "%u.%u", (unsigned)(h / 100U), (unsigned)((h % 100U) / 10U));
	}
	else
	{
		snprintf(value, size, "%u", (unsigned)MIN((h / 100U), 99U)); // 3 characters at most
	}

	if (h <= 200U)
	{
		*word = "GOOD";
		return THEME_ITEM_FG_RSSI_BAR;
	}

	if (h <= 500U)
	{
		*word = "FAIR";
		return THEME_ITEM_FG_RSSI_BAR_S9P;
	}

	*word = "POOR";
	return THEME_ITEM_FG_TX_COUNTER;
}

static uint32_t emSearchSecs(void)
{
	return ((ticksGetMillis() - gpsSearchStartMs()) / 1000U);
}

// Title bar (page number from menuDisplayTitle()) and the status strip: counts per constellation
// ("GPS 8/10": 8 with a signal, 10 in view), each with its legend symbol, or a "SATS" chip.
static void emFrame(bool withLegend, emSymbol_t gpsSymbol, emSymbol_t bdSymbol, const char *right)
{
	char buf[12];
	int16_t x = 2;

	displayClearBuf();
	menuDisplayTitle(currentLanguage->gps);

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EM_STRIP_Y, DISPLAY_SIZE_X, EM_STRIP_H, false);

	if (emGpsIsOn() == false)
	{
		return;
	}

	for (uint8_t c = 0; c < 2; c++)
	{
		if (withLegend)
		{
			emSymbol(x, (EM_STRIP_Y + 2), ((c == 0) ? gpsSymbol : bdSymbol), true,
					((c == 0) ? THEME_ITEM_FG_GPS_COLOUR : THEME_ITEM_FG_BD_COLOUR), THEME_ITEM_BG_HEADER_TEXT);
			x += 9;
		}
		else if (c == 0)
		{
			x = (int16_t)(x + emChip(x, (EM_STRIP_Y + 1), "SATS", THEME_ITEM_FG_RX_FREQ) + 4);
		}

		snprintf(buf, sizeof(buf), "%s %u/%u", ((c == 0) ? "GPS" : "BD"), (unsigned)emSatsHeard(c != 0), (unsigned)emSatsInView(c != 0));
		emText(x, (EM_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		x = (int16_t)(x + emTextWidth(buf, FONT_SIZE_1) + 6);
	}

	if (right)
	{
		emTextRight((DISPLAY_SIZE_X - 3), (EM_STRIP_Y + 2), right, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
	}
}

// GPS off / not detected: say so, with the key that turns it on
static void emOffBody(void)
{
	themeItem_t colour;
	const char *state = emFixState(&colour);
	int16_t y = (EM_TOP + 3);

	emBigChip(2, y, state, colour);

	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) == GPS_NOT_DETECTED)
	{
		emText(4, (y + 30), "No GPS receiver found.", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		emText(4, (y + 42), "Checked at power on.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emKeyBar("UP/DN PAGE  RED EXIT");
	}
	else
	{
		emText(4, (y + 30), "The receiver is off.", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		emText(4, (y + 42), "SK2+GREEN turns it on", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emText(4, (y + 52), "(or Options > General).", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emKeyBar("SK2+GRN GPS ON  RED EXIT");
	}
}

// Status page: fix state, UTC or search time, position, locator, altitude, HDOP, speed, course, TTFF
static void emStatusPage(void)
{
	char buf[24];
	char value[12];
	themeItem_t colour;
	const char *word;
	const char *state;
	bool hasFix;
	int16_t y = (EM_TOP + 3);

	emFrame(false, EM_SYM_SOLID, EM_SYM_SOLID, NULL);

	if (emGpsIsOn() == false)
	{
		emOffBody();
		return;
	}

	state = emFixState(&colour);
	hasFix = ((gpsData.Status & GPS_STATUS_HAS_FIX) != 0);
	emBigChip(2, y, state, colour);

	if (hasFix)
	{
		if (gpsData.Status & GPS_STATUS_HAS_TIME)
		{
			struct tm t;
			time_t_custom tt = gpsData.Time;

			gmtime_r_Custom(&tt, &t);
			snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)t.tm_hour, (unsigned)t.tm_min, (unsigned)t.tm_sec);
		}
		else
		{
			snprintf(buf, sizeof(buf), "--:--:--");
		}
		emText(68, (y + 5), "UTC", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emTextRight((DISPLAY_SIZE_X - 2), (y + 1), buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	}
	else
	{
		// time since the receiver was turned on / lost its fix
		emDuration(buf, sizeof(buf), emSearchSecs());
		emTextRight((DISPLAY_SIZE_X - 2), (y + 1), buf, FONT_SIZE_3, THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		if (strlen(buf) <= 7) // not beside a 10 hour search, the fix field is there
		{
			emTextRight((DISPLAY_SIZE_X - 2 - emTextWidth(buf, FONT_SIZE_3) - 4), (y + 5), "SEARCH", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		}
	}

	// Position (the GPS one, not the location setting)
	y += 21;
	emText(2, (y + 5), "LAT", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText(2, (y + 21), "LON", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText(106, (y + 1), "LOC", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText(106, (y + 21), "ALT", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

	if (hasFix)
	{
		double lat = gpsData.LatitudeHiRes;
		double lon = gpsData.LongitudeHiRes;
		uint32_t la = (uint32_t)((fabs(lat) * 100000.0) + 0.5);
		uint32_t lo = (uint32_t)((fabs(lon) * 100000.0) + 0.5);
		char loc[12] = { 0 };

		snprintf(buf, sizeof(buf), "%u.%05u%c", (unsigned)(la / 100000U), (unsigned)(la % 100000U), ((lat >= 0.0) ? 'N' : 'S'));
		emText(22, y, buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		snprintf(buf, sizeof(buf), "%u.%05u%c", (unsigned)(lo / 100000U), (unsigned)(lo % 100000U), ((lon >= 0.0) ? 'E' : 'W'));
		emText(22, (y + 16), buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);

		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		emText(110, (y + 10), loc, FONT_SIZE_2, THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	}
	else
	{
		emText(22, y, "--", FONT_SIZE_3, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emText(22, (y + 16), "--", FONT_SIZE_3, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		emText(110, (y + 10), "--", FONT_SIZE_2, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	}

	if (hasFix)
	{
		snprintf(buf, sizeof(buf), "%dm", (int)gpsData.HeightInM);
		if (strlen(buf) > 5)
		{
			buf[strlen(buf) - 1] = 0; // "12345" (no unit) up to "-9999m"
		}
		emTextRight((DISPLAY_SIZE_X - 2), (y + 21), buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	}
	else
	{
		emTextRight((DISPLAY_SIZE_X - 2), (y + 21), "--", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	}

	y += 35;
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, y, DISPLAY_SIZE_X, true);
	y += 3;

	// HDOP + word, speed
	colour = emHdop(value, sizeof(value), &word);
	snprintf(buf, sizeof(buf), "%s %s", value, word);
	emKeyValue(2, y, "HDOP", buf, colour, false);

	bool moving = (hasFix && (gpsData.SpeedInHundredthKn > GPS_SPEED_THRESHOLD_MIN));

	if (hasFix)
	{
		snprintf(buf, sizeof(buf), "%ukm/h", (unsigned)MIN((uint32_t)(((gpsData.SpeedInHundredthKn * 1852U) + 50000U) / 100000U), 999U));
	}
	else
	{
		snprintf(buf, sizeof(buf), "--");
	}
	emKeyValue((DISPLAY_SIZE_X - 2), y, "SPD", buf, THEME_ITEM_FG_DEFAULT, true);

	// Course (only when moving), time to first fix
	y += 10;
	if (moving)
	{
		int course = ((int)((gpsData.CourseInHundredthDeg / 100U) % 360U));

		snprintf(buf, sizeof(buf), "%03d %s", course, directions[((course * 10 + 112) / 225) % 16]);
	}
	else
	{
		snprintf(buf, sizeof(buf), "--");
	}
	emKeyValue(2, y, "CRS", buf, THEME_ITEM_FG_DEFAULT, false);

	if (gpsLastTtffSecs() != 0U)
	{
		emDuration(buf, sizeof(buf), gpsLastTtffSecs());
		emKeyValue((DISPLAY_SIZE_X - 2), y, "TTFF", buf, THEME_ITEM_FG_DEFAULT, true);
	}

	// Date (GPS, UTC)
	y += 10;
	if (gpsData.Status & GPS_STATUS_HAS_TIME)
	{
		struct tm t;
		time_t_custom tt = gpsData.Time;

		gmtime_r_Custom(&tt, &t);
		snprintf(buf, sizeof(buf), "%04u-%02u-%02u", (unsigned)(t.tm_year + 1900), (unsigned)(t.tm_mon + 1), (unsigned)t.tm_mday);
		emKeyValue(2, y, "DATE", buf, THEME_ITEM_FG_DEFAULT, false);
	}
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) == GPS_MODE_ON_NMEA)
	{
		emTextRight((DISPLAY_SIZE_X - 2), y, "NMEA>USB", FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	}

	emKeyBar("UP/DN PAGE  SK2+RED OFF");
}

static int16_t emSnrY(int8_t snr)
{
	return (int16_t)(EM_CH_BASE - ((MIN(snr, EM_CH_MAX_DB) * (EM_CH_BASE - EM_CH_TOP)) / EM_CH_MAX_DB));
}

// Signal page: one bar per satellite, strongest first, PRN under each bar. GPS bars are solid,
// BeiDou bars hatched (and in their colours); a satellite in view without a signal is a hollow stub.
static void emSignalPage(void)
{
	uint8_t codes[EM_CH_MAX_BARS];
	uint8_t total;
	uint8_t n = emSortedSats(codes, EM_CH_MAX_BARS, &total);
	char buf[28];

	emFrame(true, EM_SYM_SOLID, EM_SYM_HATCH, "dBHz");

	if (emGpsIsOn() == false)
	{
		emOffBody();
		return;
	}

	emKeyBar("UP/DN PAGE  RED EXIT");

	if (n == 0)
	{
		emText(((DISPLAY_SIZE_X - emTextWidth("No satellites yet", FONT_SIZE_3)) / 2), 48, "No satellites yet", FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		if ((gpsData.Status & GPS_STATUS_HAS_FIX) == 0)
		{
			char t[12];

			emDuration(t, sizeof(t), emSearchSecs());
			snprintf(buf, sizeof(buf), "Searching: %s", t);
			emText(((DISPLAY_SIZE_X - emTextWidth(buf, FONT_SIZE_1)) / 2), 70, buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		}
		emText(((DISPLAY_SIZE_X - emTextWidth("Open sky helps (1-5 min)", FONT_SIZE_1)) / 2), 82, "Open sky helps (1-5 min)", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		return;
	}

	// SNR scale, dotted every 10 dB-Hz, 30 (a usable signal) denser
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	for (int db = 10; db <= EM_CH_MAX_DB; db += 10)
	{
		int16_t y = emSnrY((int8_t)db);

		for (int16_t x = EM_CH_X; x < EM_CH_RIGHT; x += ((db == 30) ? 2 : 4))
		{
			displaySetPixel(x, y, true);
		}

		if ((db == 20) || (db == 40))
		{
			snprintf(buf, sizeof(buf), "%d", db);
			emText(1, (y - 3), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		}
	}
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine((EM_CH_X - 1), EM_CH_BASE, (EM_CH_RIGHT - EM_CH_X + 1), true);

	int16_t pitch = MIN(EM_CH_MAX_PITCH, ((EM_CH_RIGHT - EM_CH_X) / n));
	int16_t barW = (int16_t)(pitch - ((pitch >= 8) ? 2 : 1));
	uint16_t maxPrn = 0;

	for (uint8_t i = 0; i < n; i++)
	{
		maxPrn = MAX(maxPrn, emSat(codes[i])->Number);
	}

	// PRN labels: one row when they fit under their bar, else two staggered rows; 3 digit
	// numbers lose their hundreds when even two rows are too narrow.
	int16_t labelW = (int16_t)(((maxPrn > 99) ? 3 : ((maxPrn > 9) ? 2 : 1)) * 6);
	bool oneRow = (pitch > labelW);
	bool cutHundreds = ((maxPrn > 99) && ((pitch * 2) <= 18));

	for (uint8_t i = 0; i < n; i++)
	{
		gpsSatellitesData_t *sat = emSat(codes[i]);
		bool bd = ((codes[i] & EM_SAT_BD) != 0);
		themeItem_t colour = (bd ? THEME_ITEM_FG_BD_COLOUR : THEME_ITEM_FG_GPS_COLOUR);
		int16_t x = (int16_t)(EM_CH_X + 1 + (i * pitch));

		if (sat->RSSI > 0)
		{
			int16_t y = emSnrY(sat->RSSI);
			int16_t h = (int16_t)(EM_CH_BASE - y);

			displayThemeApply(colour, THEME_ITEM_BG);
			if (bd)
			{
				displayDrawRect(x, y, barW, h, true);
				for (int16_t yy = (int16_t)(y + 2); yy < EM_CH_BASE; yy += 2)
				{
					displayDrawFastHLine(x, yy, barW, true);
				}
			}
			else
			{
				displayFillRect(x, y, barW, h, false);
			}

			if (oneRow && (pitch >= 13))
			{
				snprintf(buf, sizeof(buf), "%d", sat->RSSI);
				emText((int16_t)(x + (barW / 2) - (emTextWidth(buf, FONT_SIZE_1) / 2)), (y - 9), buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
			}
		}
		else
		{
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayDrawRect(x, (EM_CH_BASE - 3), barW, 3, true);
		}

		snprintf(buf, sizeof(buf), "%u", (unsigned)(cutHundreds ? (sat->Number % 100U) : sat->Number));
		emText((int16_t)MAX(0, (x + (barW / 2) - (emTextWidth(buf, FONT_SIZE_1) / 2))), (EM_CH_BASE + 2 + ((oneRow || ((i & 1) == 0)) ? 0 : 8)), buf, FONT_SIZE_1,
				((sat->RSSI > 0) ? colour : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG);
	}

	if (total > n)
	{
		snprintf(buf, sizeof(buf), "+%u", (unsigned)(total - n));
		emTextRight(EM_CH_RIGHT, (EM_TOP + 1), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	}
}

// Position of a satellite on the sky plot; false without elevation / azimuth
static bool emSkyPoint(const gpsSatellitesData_t *sat, uint8_t *x, uint8_t *y)
{
	if ((sat->Number == 0) || (sat->El < 0) || (sat->Az < 0))
	{
		return false;
	}

	float f = ((1.0f - (MIN(sat->El, 90) / 90.0f)) * EM_SKY_R);
	float a = (sat->Az * (float)DEG_TO_RAD);

	*x = (uint8_t)(EM_SKY_CX + lroundf(sinf(a) * f));
	*y = (uint8_t)(EM_SKY_CY - lroundf(cosf(a) * f));
	return true;
}

static bool emBoxesOverlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah, int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
	return ((ax < (bx + bw)) && (bx < (ax + aw)) && (ay < (by + bh)) && (by < (ay + ah)));
}

// Sky plot: horizon, 30 and 60 degree rings, N up. GPS = discs, BeiDou = squares, filled with a
// signal, hollow without; PRN numbers beside the satellites with a signal where they do not collide.
static void emSkyPage(void)
{
	uint8_t codes[EM_SKY_MAX_SATS];
	uint8_t px[EM_SKY_MAX_SATS]; // the plot is inside the screen: 0..159, 0..127
	uint8_t py[EM_SKY_MAX_SATS];
	uint8_t labels[EM_SKY_MAX_LABELS][3]; // x, y, width
	uint8_t numLabels = 0;
	uint8_t total;
	uint8_t n = emSortedSats(codes, EM_SKY_MAX_SATS, &total);
	uint8_t plotted = 0;
	char buf[12];
	char value[8];
	themeItem_t colour;
	const char *word;
	const char *state;

	emFrame(true, EM_SYM_DISC, EM_SYM_SOLID, "N UP");

	if (emGpsIsOn() == false)
	{
		emOffBody();
		return;
	}

	emKeyBar("UP/DN PAGE  RED EXIT");

	// Grid
	displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);
	displayDrawCircle(EM_SKY_CX, EM_SKY_CY, EM_SKY_R, true);
	displayDrawCircle(EM_SKY_CX, EM_SKY_CY, ((EM_SKY_R * 2) / 3), true);
	displayDrawCircle(EM_SKY_CX, EM_SKY_CY, (EM_SKY_R / 3), true);
	displayDrawFastVLine(EM_SKY_CX, (EM_SKY_CY - EM_SKY_R), ((EM_SKY_R * 2) + 1), true);
	displayDrawFastHLine((EM_SKY_CX - EM_SKY_R), EM_SKY_CY, ((EM_SKY_R * 2) + 1), true);

	// Cardinals inside the horizon ring
	emText((EM_SKY_CX - 2), (EM_SKY_CY - EM_SKY_R + 2), directions[0], FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	emText((EM_SKY_CX - 2), (EM_SKY_CY + EM_SKY_R - 9), directions[8], FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText((EM_SKY_CX + EM_SKY_R - 7), (EM_SKY_CY - 3), directions[4], FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText((EM_SKY_CX - EM_SKY_R + 3), (EM_SKY_CY - 3), directions[12], FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

	// Positions (codes[] is strongest first; satellites without a position are dropped)
	for (uint8_t i = 0; i < n; i++)
	{
		if (emSkyPoint(emSat(codes[i]), &px[plotted], &py[plotted]))
		{
			codes[plotted++] = codes[i];
		}
	}

	// Markers, weakest first so the strong ones stay on top
	for (int i = (plotted - 1); i >= 0; i--)
	{
		bool bd = ((codes[i] & EM_SAT_BD) != 0);

		emSymbol((px[i] - 3), (py[i] - 3), (bd ? EM_SYM_SOLID : EM_SYM_DISC), (emSat(codes[i])->RSSI > 0),
				(bd ? THEME_ITEM_FG_BD_COLOUR : THEME_ITEM_FG_GPS_COLOUR), THEME_ITEM_BG);
	}

	// Ring elevations on the SE diagonal, else the SW one, where no satellite is
	for (uint8_t ring = 1; ring <= 2; ring++)
	{
		int16_t d = (int16_t)(((EM_SKY_R * ring) / 3) * 707 / 1000);
		int16_t ly = (int16_t)(EM_SKY_CY + d - 1);

		for (uint8_t side = 0; side < 2; side++)
		{
			int16_t lx = (int16_t)((side == 0) ? (EM_SKY_CX + d + 1) : (EM_SKY_CX - d - 12));
			bool isFree = true;

			for (uint8_t k = 0; isFree && (k < plotted); k++)
			{
				isFree = !emBoxesOverlap(lx, ly, 12, 8, (px[k] - 3), (py[k] - 3), 7, 7);
			}

			if (isFree)
			{
				emText(lx, ly, ((ring == 1) ? "60" : "30"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
				labels[numLabels][0] = (uint8_t)lx;
				labels[numLabels][1] = (uint8_t)ly;
				labels[numLabels][2] = 12;
				numLabels++;
				break;
			}
		}
	}

	// PRN labels, strongest first, right of the marker else left, when free
	for (uint8_t i = 0; (i < plotted) && (numLabels < EM_SKY_MAX_LABELS); i++)
	{
		gpsSatellitesData_t *sat = emSat(codes[i]);

		if (sat->RSSI <= 0)
		{
			break; // the rest has no signal either
		}

		snprintf(buf, sizeof(buf), "%u", (unsigned)sat->Number);
		int16_t w = emTextWidth(buf, FONT_SIZE_1);
		int16_t ly = (int16_t)(py[i] - 4);

		for (uint8_t side = 0; side < 2; side++)
		{
			int16_t lx = (int16_t)((side == 0) ? (px[i] + 5) : (px[i] - 4 - w));
			bool isFree = ((lx >= 0) && ((lx + w) <= EM_SKY_RIGHT) && (ly > EM_TOP) && ((ly + 8) < EM_KEYBAR_Y));

			for (uint8_t k = 0; isFree && (k < numLabels); k++)
			{
				isFree = !emBoxesOverlap(lx, ly, w, 8, labels[k][0], labels[k][1], labels[k][2], 8);
			}

			for (uint8_t k = 0; isFree && (k < plotted); k++)
			{
				isFree = ((k == i) || !emBoxesOverlap(lx, ly, w, 8, (px[k] - 3), (py[k] - 3), 7, 7));
			}

			if (isFree)
			{
				emText(lx, ly, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
				labels[numLabels][0] = (uint8_t)lx;
				labels[numLabels][1] = (uint8_t)ly;
				labels[numLabels][2] = (uint8_t)w;
				numLabels++;
				break;
			}
		}
	}

	if (plotted == 0)
	{
		emText((EM_SKY_CX - (emTextWidth("no satellites", FONT_SIZE_1) / 2)), (EM_SKY_CY + 8), "no satellites", FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
	}

	// Right column: fix, HDOP, legend
	state = emFixState(&colour);
	emChip(EM_SKY_COL_X, (EM_TOP + 3), state, colour);

	emText(EM_SKY_COL_X, (EM_TOP + 17), "HDOP", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	colour = emHdop(value, sizeof(value), &word);
	emText(EM_SKY_COL_X, (EM_TOP + 26), value, FONT_SIZE_3, colour, THEME_ITEM_BG);
	emText((EM_SKY_COL_X + 28), (EM_TOP + 31), word, FONT_SIZE_1, colour, THEME_ITEM_BG);

	emSymbol(EM_SKY_COL_X, (EM_TOP + 50), EM_SYM_DISC, true, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	emText((EM_SKY_COL_X + 10), (EM_TOP + 50), "signal", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	emSymbol(EM_SKY_COL_X, (EM_TOP + 60), EM_SYM_DISC, false, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	emText((EM_SKY_COL_X + 10), (EM_TOP + 60), "no sig", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText(EM_SKY_COL_X, (EM_TOP + 74), "rings", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	snprintf(buf, sizeof(buf), "0/30/60%c", 176);
	emText(EM_SKY_COL_X, (EM_TOP + 83), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
}

static bool emIsEmcomPage(void)
{
	return (emcomStyleEnabled() &&
			((menuDataGlobal.currentItemIndex == PAGE_STATUS) || (menuDataGlobal.currentItemIndex == PAGE_RSSI) || (menuDataGlobal.currentItemIndex == PAGE_POLAR)));
}

static void emDrawPage(void)
{
	switch (menuDataGlobal.currentItemIndex)
	{
		case PAGE_STATUS:
			emStatusPage();
			break;
		case PAGE_RSSI:
			emSignalPage();
			break;
		default:
			emSkyPage();
			break;
	}
	displayThemeResetToDefault();
}
#endif

static bool displayGPSData(bool isFirstRun, bool forceRedraw)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];
	bool res = false;

	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF)
	{
#if defined(HAS_EMCOM_COLOUR_UI)
		if (emIsEmcomPage())
		{
			// Fork: status, signal and sky pages in the EMCOM style draw their whole screen
			emDrawPage();
			res = true;
		}
		else
#endif
		switch(menuDataGlobal.currentItemIndex)
		{
#if defined(HAS_EMCOM_COLOUR_UI)
			case PAGE_STATUS: // EMCOM style only, drawn above
				break;
#endif
			case PAGE_COORDS: // with PAGE_RSSI on MD-UV380
				res = displayCoords(isFirstRun, forceRedraw);
#if !(defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12))
				res |= displayRSSI(isFirstRun, forceRedraw);
#endif
				break;

#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			case PAGE_RSSI:
				res = displayRSSI(isFirstRun, forceRedraw);
				break;
#endif

			case PAGE_POLAR:
				res = displayPolar(isFirstRun, forceRedraw);
				break;

			case PAGE_DIRECTION:
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			case PAGE_DIRECTION_2:
#endif
				res = displayDirectionInfo(isFirstRun, forceRedraw);
				break;

#if defined(GPS_WAYPOINT_PAGE)
			case PAGE_WAYPOINT:
				res = displayWaypoint(isFirstRun, forceRedraw);
				break;
#endif

#if defined(GPS_TRIP_COMPUTER)
			case PAGE_TRIP:
				res = displayTrip(isFirstRun, forceRedraw);
				break;
#endif

			case PAGES_MAX:
				break;
		}

		//
		// Update the Voice Prompts
		//
		if (gpsData.Status & GPS_STATUS_HAS_FIX)
		{
			bool vpIsPlaying = voicePromptsIsPlaying();
			uint16_t inViewAndUsableSats = getTotalNumberOfUsableSats();

			if (((isFirstRun || vpIsPlaying) == false) &&
					((prevSatsInView != inViewAndUsableSats) || (gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_POSITION_UPDATED | GPS_STATUS_HEIGHT_UPDATED))))
			{
				prevSatsInView = inViewAndUsableSats;

				if (gpsData.Status & GPS_STATUS_FIX_UPDATED)
				{
					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						voicePromptsInitWithOverride();
					}
					else
					{
						soundSetMelody(MELODY_ACK_BEEP);
					}
				}
				else
				{
					voicePromptsInit();
				}

				if (!vpIsPlaying)
				{
					if (gpsData.Status & GPS_STATUS_HAS_POSITION)
					{
						char locationBuffer[LOCATION_TEXT_BUFFER_SIZE];
						char maidenheadBuf[7];

						buildLocationAndMaidenheadStrings(locationBuffer, maidenheadBuf, true);

						voicePromptsAppendLanguageString(currentLanguage->location);
						voicePromptsAppendString(locationBuffer);
						voicePromptsAppendPrompt(PROMPT_SILENCE);
						voicePromptsAppendPrompt(PROMPT_SILENCE);
						voicePromptsAppendPrompt(PROMPT_SILENCE);
						voicePromptsAppendString(maidenheadBuf);
					}

					if (prevSatsInView != 0xFFFF)
					{
						voicePromptsAppendLanguageString(currentLanguage->satellite);
						voicePromptsAppendInteger(prevSatsInView);
					}

					if (gpsData.Status & GPS_STATUS_HAS_HEIGHT)
					{
						voicePromptsAppendLanguageString(currentLanguage->altitude);
						voicePromptsAppendInteger(gpsData.HeightInM);
					}
				}
			}
		}
		else
		{
			//                               |--> fix got lost
			if (isFirstRun || forceRedraw || (gpsData.Status & GPS_STATUS_FIX_UPDATED))
			{
				prevSatsInView = 0xFFFF;

#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
				if (menuDataGlobal.currentItemIndex < PAGE_RSSI)   // Do not display anything on RSSI, Polar and Direction* screens
#else
				if ((menuDataGlobal.currentItemIndex < PAGE_POLAR)  // Do not display anything on Polar and Direction* screens
#if defined(HAS_EMCOM_COLOUR_UI)
						&& (emIsEmcomPage() == false) // the EMCOM pages show the search state themselves
#endif
						)
#endif
				{
					// Update, only clear screen regions
					if (((isFirstRun || forceRedraw) == false) && (gpsData.Status & (GPS_STATUS_FIX_UPDATED)))
					{
						displayFillRect(0, 16, DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
						displayFillRect(0, 32, DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
#endif
						displayFillRect(0, MAIDENHEAD_HDOP_Y_POS, DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
					}

					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s", currentLanguage->gps_acquiring);
					displayPrintCentered(16, buffer, FONT_SIZE_3);
				}

				if (!isFirstRun)
				{
					if (gpsData.Status & GPS_STATUS_FIX_UPDATED)
					{
						if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
						{
							voicePromptsInitWithOverride();
						}
						else
						{
							soundSetMelody(MELODY_NACK_BEEP);
						}
					}
					else
					{
						voicePromptsInit();
					}
				}

				voicePromptsAppendLanguageString(currentLanguage->gps_acquiring);

				res = true;
			}

		}
	}
	else
	{
		if (isFirstRun || forceRedraw || (prevGPSState != SETTINGS_GPS_MODE_GET(nonVolatileSettings)))
		{
#if defined(HAS_EMCOM_COLOUR_UI)
			const bool emPage = emIsEmcomPage();

			if (emPage)
			{
				emDrawPage(); // GPS off / not detected, with the key to turn it on
			}
			else
#endif
			if ((isFirstRun || forceRedraw) == false) // only clear if the GPS power status has changed
			{
				displayFillRect(0, (((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1) + (FONT_SIZE_3_HEIGHT >> 1)), DISPLAY_SIZE_X, FONT_SIZE_3_HEIGHT, true);
			}

			if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) == GPS_MODE_OFF)
			{
#if defined(HAS_EMCOM_COLOUR_UI)
				if (emPage == false)
#endif
				{
					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s %s", currentLanguage->gps, currentLanguage->off);
					displayThemeApply(THEME_ITEM_FG_WARNING_NOTIFICATION, THEME_ITEM_BG);
					displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1) + (FONT_SIZE_3_HEIGHT >> 1), buffer, FONT_SIZE_3);
					displayThemeResetToDefault();
				}

				if (forceRedraw == false)
				{
					voicePromptsAppendLanguageString(currentLanguage->gps);
					voicePromptsAppendLanguageString(currentLanguage->off);
				}
			}
			else
			{
#if defined(HAS_EMCOM_COLOUR_UI)
				if (emPage == false)
#endif
				{
					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s %s", currentLanguage->no, currentLanguage->gps);
					displayThemeApply(THEME_ITEM_FG_ERROR_NOTIFICATION, THEME_ITEM_BG);
					displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1) + (FONT_SIZE_3_HEIGHT >> 1), buffer, FONT_SIZE_3);
					displayThemeResetToDefault();
				}

				if (forceRedraw == false)
				{
					voicePromptsAppendLanguageString(currentLanguage->no);
					voicePromptsAppendLanguageString(currentLanguage->gps);
				}
			}

			res = true;
		}
	}

	// First run
	if ((isFirstRun || (gpsData.Status & GPS_STATUS_FIX_UPDATED)) && (voicePromptsIsPlaying() == false))
	{
		promptsPlayNotAfterTx();
	}

	return res;
}

static void updateScreen(bool isFirstRun, bool forceRedraw)
{
	bool res;

	if (isFirstRun || forceRedraw)
	{
		char buffer[SCREEN_LINE_BUFFER_SIZE];

		displayClearBuf();
		menuDisplayTitle(currentLanguage->gps);

#if defined(HAS_EMCOM_COLOUR_UI)
		if (emcomStyleEnabled() == false) // the EMCOM title bar shows the page number itself
#endif
		{
			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d/%d", (menuDataGlobal.currentItemIndex - gpsFirstPage() + 1), (PAGES_MAX - gpsFirstPage()));
			displayThemeApply(THEME_ITEM_FG_MENU_NAME, THEME_ITEM_BG);
			displayPrintCore(0, 3, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
			displayThemeResetToDefault();
		}
	}

	res = displayGPSData(isFirstRun, forceRedraw);

	if (isFirstRun || forceRedraw || res)
	{
		displayRender();
	}

	// GPS power mode (Off/On/NMEA) has changed, display update is now done, store the new state.
	if (prevGPSState != SETTINGS_GPS_MODE_GET(nonVolatileSettings))
	{
		prevGPSState = SETTINGS_GPS_MODE_GET(nonVolatileSettings);
	}

	// Clear all the *_UPDATED flags.
	gpsData.Status &= ~(GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED | GPS_STATUS_POSITION_UPDATED |
			GPS_STATUS_HDOP_UPDATED | GPS_STATUS_COURSE_UPDATED | GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HEIGHT_UPDATED |
			GPS_STATUS_GPS_SATS_UPDATED | GPS_STATUS_BD_SATS_UPDATED);
}

static void handleEvent(uiEvent_t *ev)
{
	bool isDirty = false;

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_REDRAW)
		{
			isDirty = true;
			goto eventHanderExit;
		}
		else if (QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU)
		{
#if defined(GPS_WAYPOINT_PAGE)
			waypointEditActive = false;
#endif
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
			if ((menuDataGlobal.currentItemIndex < gpsFirstPage()) || (menuDataGlobal.currentItemIndex >= PAGES_MAX))
			{
				menuDataGlobal.currentItemIndex = gpsFirstPage();
			}
			isDirty = true;
			goto eventHanderExit;
		}
	}

#if defined(GPS_WAYPOINT_PAGE)
	if (waypointEditActive)
	{
		double lat, lon;
		gpsWaypointEdit_t r = gpsWaypointEditHandleEvent(ev, &lat, &lon);

		if (r == GPS_WP_EDIT_DONE)
		{
			gpsWaypointSet(lat, lon);
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000U, "Waypoint set", false);
		}
		if (r != GPS_WP_EDIT_NONE)
		{
			updateScreen(false, true);
		}
		return;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH) && (menuDataGlobal.currentItemIndex == PAGE_WAYPOINT))
	{
		gpsWaypointEditStart(true); // pre-filled with the current waypoint so it can be tweaked
		isDirty = true;
		goto eventHanderExit;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) && (menuDataGlobal.currentItemIndex == PAGE_WAYPOINT))
	{
		menuSystemPushNewMenu(MENU_WAYPOINTS); // the saved waypoints
		return;
	}
#endif

	if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
	{
		if (menuDataGlobal.currentItemIndex > gpsFirstPage())
		{
			menuDataGlobal.currentItemIndex--;
			isDirty = true;
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
	{
		if (menuDataGlobal.currentItemIndex < (PAGES_MAX - 1))
		{
			menuDataGlobal.currentItemIndex++;
			isDirty = true;
		}
	}
	else if ((KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
#if defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12)
			// Let the operator to set the GPS power status to GPS_MODE_ON only, on longpress GREEN (A/B mic button also).
			|| (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN) && (SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_NOT_DETECTED) && (SETTINGS_GPS_MODE_GET(nonVolatileSettings) < GPS_MODE_ON))
#endif
	)
	{
		gpsOnUsingQuickKey(true);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		gpsOnUsingQuickKey(false);
	}
#if defined(GPS_WAYPOINT_PAGE)
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (menuDataGlobal.currentItemIndex == PAGE_WAYPOINT))
	{
		if (waypointMarkHere())
		{
			soundSetMelody(MELODY_ACK_BEEP);
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000U, "Waypoint set", false);
			isDirty = true;
		}
		else
		{
			soundSetMelody(MELODY_NACK_BEEP);
		}
	}
#endif
#if defined(GPS_TRIP_COMPUTER)
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN) && (menuDataGlobal.currentItemIndex == PAGE_TRIP))
	{
		// Long press so a stray GREEN does not wipe a day of hiking
		gpsTripReset();
		soundSetMelody(MELODY_ACK_BEEP);
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000U, "Trip reset", false);
		isDirty = true;
	}
#endif
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, 0);
	}

	eventHanderExit:

	if (isDirty)
	{
		updateScreen(false, true);
	}
}

#if defined(GPS_WAYPOINT_PAGE)
#define WAYPOINT_ARROW_CX      40
#define WAYPOINT_ARROW_CY      (MENU_HEADER_HEIGHT + ((DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT) / 2))
#define WAYPOINT_ARROW_R       30
#define WAYPOINT_TEXT_X        80

static bool waypointPositionIsValid(void)
{
	return ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION));
}

static bool waypointMarkHere(void)
{
	if (waypointPositionIsValid() == false)
	{
		return false;
	}

	waypointLat = gpsData.LatitudeHiRes;
	waypointLon = gpsData.LongitudeHiRes;
	waypointIsSet = true;

	return true;
}

// Arrow pointing at angleDeg (0 = up, clockwise).
static void waypointDrawArrow(double angleDeg)
{
	double a = angleDeg * DEG_TO_RAD;
	double aL = (angleDeg + 150.0) * DEG_TO_RAD;
	double aR = (angleDeg - 150.0) * DEG_TO_RAD;
	int16_t tipX = WAYPOINT_ARROW_CX + (int16_t)(sin(a) * (WAYPOINT_ARROW_R - 3));
	int16_t tipY = WAYPOINT_ARROW_CY - (int16_t)(cos(a) * (WAYPOINT_ARROW_R - 3));
	int16_t lX = WAYPOINT_ARROW_CX + (int16_t)(sin(aL) * (WAYPOINT_ARROW_R - 8));
	int16_t lY = WAYPOINT_ARROW_CY - (int16_t)(cos(aL) * (WAYPOINT_ARROW_R - 8));
	int16_t rX = WAYPOINT_ARROW_CX + (int16_t)(sin(aR) * (WAYPOINT_ARROW_R - 8));
	int16_t rY = WAYPOINT_ARROW_CY - (int16_t)(cos(aR) * (WAYPOINT_ARROW_R - 8));

	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
	displayFillTriangle(tipX, tipY, lX, lY, WAYPOINT_ARROW_CX, WAYPOINT_ARROW_CY, true);
	displayFillTriangle(tipX, tipY, rX, rY, WAYPOINT_ARROW_CX, WAYPOINT_ARROW_CY, true);
	displayThemeResetToDefault();
}

static bool displayWaypoint(bool isFirstRun, bool forceRedraw)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	if (waypointEditActive && ((isFirstRun || forceRedraw) == false))
	{
		return false; // the editor only changes on key presses
	}

	if (((isFirstRun || forceRedraw) == false) &&
			((gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_POSITION_UPDATED | GPS_STATUS_COURSE_UPDATED | GPS_STATUS_SPEED_UPDATED)) == 0))
	{
		return false;
	}

	// Clear everything below the title
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT), true);

	if (waypointEditActive)
	{
		gpsWaypointEditDraw();
		return true;
	}

	if (waypointIsSet == false)
	{
		displayPrintCentered((WAYPOINT_ARROW_CY - FONT_SIZE_3_HEIGHT), "No waypoint", FONT_SIZE_3);
		displayPrintCentered((WAYPOINT_ARROW_CY + 4), "GREEN: mark here", FONT_SIZE_1);
		displayPrintCentered((WAYPOINT_ARROW_CY + 14), "#: enter lat/lon", FONT_SIZE_1);
		displayPrintCentered((WAYPOINT_ARROW_CY + 24), "*: saved list", FONT_SIZE_1);
		return true;
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawCircle(WAYPOINT_ARROW_CX, WAYPOINT_ARROW_CY, WAYPOINT_ARROW_R, true);
	displayThemeResetToDefault();

	if (waypointPositionIsValid() == false)
	{
		displayPrintAt(WAYPOINT_TEXT_X, (WAYPOINT_ARROW_CY - 4), "No fix", FONT_SIZE_3);
		return true;
	}

	double lat = gpsData.LatitudeHiRes;
	double lon = gpsData.LongitudeHiRes;
	int meters = (int)distanceBetweenTwoCoords(lat, lon, waypointLat, waypointLon, false);
	double bearing = courseTo(lat, lon, waypointLat, waypointLon);
	bool moving = (((gpsData.Status & (GPS_STATUS_HAS_COURSE | GPS_STATUS_HAS_SPEED)) == (GPS_STATUS_HAS_COURSE | GPS_STATUS_HAS_SPEED)) &&
			(gpsData.SpeedInHundredthKn >= GPS_SPEED_THRESHOLD_MIN));
	double arrow = bearing;

	if (moving)
	{
		arrow = bearing - (gpsData.CourseInHundredthDeg / 100.0);
	}

	if (meters >= 5)
	{
		waypointDrawArrow(arrow);
	}
	else
	{
		// Arrived: fill the centre instead of an arrow
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
		displayFillCircle(WAYPOINT_ARROW_CX, WAYPOINT_ARROW_CY, 8, true);
		displayThemeResetToDefault();
	}

	// Distance
	if (meters < 1000)
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%dm", meters);
	}
	else if (meters < 100000)
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d.%01dkm", (meters / 1000), ((meters % 1000) / 100));
	}
	else
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%dkm", (meters / 1000));
	}
	displayPrintAt(WAYPOINT_TEXT_X, (WAYPOINT_ARROW_CY - 26), buffer, FONT_SIZE_3);

	// Bearing + cardinal
	int b = (((int)(bearing + 0.5)) % 360);
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%03d %s", b, directions[((b * 10 + 112) / 225) % 16]);
	displayPrintAt(WAYPOINT_TEXT_X, (WAYPOINT_ARROW_CY - 6), buffer, FONT_SIZE_3);

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(WAYPOINT_TEXT_X, (WAYPOINT_ARROW_CY + 16), (moving ? "Ahead=up" : "North=up"), FONT_SIZE_1);
	displayPrintAt(WAYPOINT_TEXT_X, (WAYPOINT_ARROW_CY + 26), "*: saved", FONT_SIZE_1);
	displayThemeResetToDefault();

	return true;
}
#endif

#if defined(GPS_WAYPOINT_PAGE)
// Fork: the GPS screen on the waypoint page (saved waypoints > GREEN)
void gpsShowWaypointPage(void)
{
	if (menuSystemGetCurrentMenuNumber() != MENU_GPS)
	{
		menuSystemPushNewMenu(MENU_GPS);
	}
	waypointEditActive = false;
	menuDataGlobal.currentItemIndex = PAGE_WAYPOINT;
	updateScreen(false, true);
}

#define WAYPOINT_EDIT_Y(n)   (MENU_HEADER_HEIGHT + 6 + ((n) * 22))

void gpsWaypointEditStart(bool prefill)
{
	if (prefill && waypointIsSet)
	{
		snprintf(waypointEditFields[0], sizeof(waypointEditFields[0]), "%.5f", waypointLat);
		snprintf(waypointEditFields[1], sizeof(waypointEditFields[1]), "%.5f", waypointLon);
	}
	else
	{
		waypointEditFields[0][0] = 0;
		waypointEditFields[1][0] = 0;
	}
	waypointEditFieldIndex = 0;
	waypointEditActive = true;
}

bool gpsWaypointEditIsActive(void)
{
	return waypointEditActive;
}

void gpsWaypointEditStop(void)
{
	waypointEditActive = false;
}

void gpsWaypointEditDraw(void)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	for (int i = 0; i < 2; i++)
	{
		bool active = (i == waypointEditFieldIndex);

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s%s%s", ((i == 0) ? "Lat " : "Lon "), waypointEditFields[i], (active ? "_" : ""));

		if (active)
		{
			displayFillRect(0, (WAYPOINT_EDIT_Y(i) - 2), DISPLAY_SIZE_X, (FONT_SIZE_3_HEIGHT + 4), false);
			displayPrintCore(2, WAYPOINT_EDIT_Y(i), buffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
		}
		else
		{
			displayPrintAt(2, WAYPOINT_EDIT_Y(i), buffer, FONT_SIZE_3);
		}
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(2, WAYPOINT_EDIT_Y(2), "# .   * -(S/W)", FONT_SIZE_1);
	displayPrintAt(2, (WAYPOINT_EDIT_Y(2) + 10), "UP/DN field RED del", FONT_SIZE_1);
	displayPrintAt(2, (WAYPOINT_EDIT_Y(2) + 20), "GREEN set", FONT_SIZE_1);
	displayThemeResetToDefault();
}

static bool waypointEditParse(const char *str, double limit, double *out)
{
	char *end = NULL;

	if ((str[0] == 0) || ((str[0] == '-') && (str[1] == 0)))
	{
		return false;
	}

	double v = strtod(str, &end);

	if ((end == str) || (*end != 0) || (v > limit) || (v < -limit))
	{
		return false;
	}

	*out = v;
	return true;
}

// GREEN with two valid fields: GPS_WP_EDIT_DONE and the position in lat / lon (the editor is closed).
gpsWaypointEdit_t gpsWaypointEditHandleEvent(uiEvent_t *ev, double *latOut, double *lonOut)
{
	if ((ev->events & KEY_EVENT) == 0)
	{
		return GPS_WP_EDIT_NONE;
	}

	char *field = waypointEditFields[waypointEditFieldIndex];
	size_t len = strlen(field);

	if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
	{
		if (len < WAYPOINT_EDIT_FIELD_LEN)
		{
			field[len] = (char)ev->keys.key;
			field[len + 1] = 0;
			return GPS_WP_EDIT_REDRAW;
		}
		soundSetMelody(MELODY_NACK_BEEP);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		if ((strchr(field, '.') == NULL) && (len < WAYPOINT_EDIT_FIELD_LEN))
		{
			field[len] = '.';
			field[len + 1] = 0;
			return GPS_WP_EDIT_REDRAW;
		}
		soundSetMelody(MELODY_NACK_BEEP);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		if (field[0] == '-')
		{
			memmove(field, (field + 1), len); // includes the terminator
		}
		else if (len < WAYPOINT_EDIT_FIELD_LEN)
		{
			memmove((field + 1), field, (len + 1));
			field[0] = '-';
		}
		return GPS_WP_EDIT_REDRAW;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
	{
		waypointEditFieldIndex ^= 1;
		return GPS_WP_EDIT_REDRAW;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (len > 0)
		{
			field[len - 1] = 0;
		}
		else
		{
			waypointEditActive = false; // RED on an empty field leaves the editor
			return GPS_WP_EDIT_CANCEL;
		}
		return GPS_WP_EDIT_REDRAW;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		double lat, lon;

		if (waypointEditParse(waypointEditFields[0], 90.0, &lat) == false)
		{
			waypointEditFieldIndex = 0;
			soundSetMelody(MELODY_NACK_BEEP);
			return GPS_WP_EDIT_REDRAW;
		}

		if (waypointEditParse(waypointEditFields[1], 180.0, &lon) == false)
		{
			waypointEditFieldIndex = 1; // jump to the longitude field (also the normal flow after typing lat)
			soundSetMelody(((waypointEditFields[1][0] == 0) ? MELODY_ACK_BEEP : MELODY_NACK_BEEP));
			return GPS_WP_EDIT_REDRAW;
		}

		*latOut = lat;
		*lonOut = lon;
		waypointEditActive = false;
		soundSetMelody(MELODY_ACK_BEEP);
		return GPS_WP_EDIT_DONE;
	}

	return GPS_WP_EDIT_NONE;
}
#endif

#if defined(GPS_TRIP_COMPUTER)
#define TRIP_LINE_Y(n)   (MENU_HEADER_HEIGHT + 4 + ((n) * 20))

static bool displayTrip(bool isFirstRun, bool forceRedraw)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	if (((isFirstRun || forceRedraw) == false) &&
			((gpsData.Status & (GPS_STATUS_FIX_UPDATED | GPS_STATUS_POSITION_UPDATED | GPS_STATUS_TIME_UPDATED | GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HEIGHT_UPDATED)) == 0))
	{
		return false;
	}

	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT), true);

	if (gpsTrip.started == false)
	{
		displayPrintCentered((TRIP_LINE_Y(1) + 4), "Trip: no fix yet", FONT_SIZE_3);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCentered((TRIP_LINE_Y(3) + 4), "Long GREEN: reset", FONT_SIZE_1);
		displayThemeResetToDefault();
		return true;
	}

	// Distance
	if (gpsTrip.distanceM < 1000U)
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Dist %um", (unsigned)gpsTrip.distanceM);
	}
	else
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Dist %u.%02ukm", (unsigned)(gpsTrip.distanceM / 1000U), (unsigned)((gpsTrip.distanceM % 1000U) / 10U));
	}
	displayPrintAt(2, TRIP_LINE_Y(0), buffer, FONT_SIZE_3);

	// Moving time
	uint32_t s = (gpsTrip.movingTimeMs / 1000U);
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Move %u:%02u:%02u", (unsigned)(s / 3600U), (unsigned)((s / 60U) % 60U), (unsigned)(s % 60U));
	displayPrintAt(2, TRIP_LINE_Y(1), buffer, FONT_SIZE_3);

	// Average moving speed
	uint32_t avg = gpsTripAvgSpeedHundredthKmh();
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Avg  %u.%01ukm/h", (unsigned)(avg / 100U), (unsigned)((avg % 100U) / 10U));
	displayPrintAt(2, TRIP_LINE_Y(2), buffer, FONT_SIZE_3);

	// Max speed (hundredth kn -> tenth km/h)
	uint32_t maxTenthKmh = (((uint32_t)gpsTrip.maxSpeedHundredthKn * 1852U) / 10000U);
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Max  %u.%01ukm/h", (unsigned)(maxTenthKmh / 10U), (unsigned)(maxTenthKmh % 10U));
	displayPrintAt(2, TRIP_LINE_Y(3), buffer, FONT_SIZE_3);

	// Altitude gain / loss
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "Alt  +%u -%um", (unsigned)gpsTrip.altGainM, (unsigned)gpsTrip.altLossM);
	displayPrintAt(2, TRIP_LINE_Y(4), buffer, FONT_SIZE_3);

	return true;
}
#endif

#endif
