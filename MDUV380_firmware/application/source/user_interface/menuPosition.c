/*
 * Fork: field position screen (main menu > Position), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Own position in MGRS (big, as read out over the radio to SAR / military teams), UTM,
 * decimal degrees and Maidenhead locator, altitude, GPS fix quality, sunrise / sunset
 * and the distance / bearing to the GPS waypoint.
 * '*' marks the current position as the waypoint (to come back to it), RED leaves.
 * Without a GPS fix the location from the settings is used (shown as MANUAL).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/fieldData.h"
#include "functions/fieldNav.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(HAS_FIELD_TOOLS)

#define REFRESH_MS   1000U
#define FOOTER_H     11

typedef enum
{
	SRC_NONE = 0,
	SRC_GPS,
	SRC_MANUAL
} posSource_t;

static posSource_t getPosition(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) &&
			((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return SRC_GPS;
	}
#endif
	if (settingsLocationIsValid())
	{
		*lat = settingsLocationGetLatitude();
		*lon = settingsLocationGetLongitude();
		if ((fabs(*lat) <= 90.0) && (fabs(*lon) <= 180.0))
		{
			return SRC_MANUAL;
		}
	}
	return SRC_NONE;
}

static void text(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg, bool rightAligned)
{
	displayThemeApply(fg, THEME_ITEM_BG);
	if (rightAligned)
	{
		int16_t w = (int16_t)(strlen(s) * ((font == FONT_SIZE_3) ? 8 : 6));
		x = (int16_t)(x - w);
	}
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static void chip(int16_t xRight, int16_t y, const char *s, themeItem_t colour)
{
	int16_t w = (int16_t)((strlen(s) * 6) + 4);

	displayThemeApply(colour, THEME_ITEM_BG_MENU_NAME);
	displayFillRect((xRight - w), y, w, 10, false);
	displayPrintCore((xRight - w + 2), (y + 1), s, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
}

// Local time offset in seconds (0 when the radio shows UTC)
static int32_t localOffset(void)
{
	return ((nonVolatileSettings.timezone & 0x80) ? (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60)) : 0);
}

static void sunLine(double lat, double lon, char *buf, size_t size)
{
	struct tm gmt;
	time_t_custom now = uiDataGlobal.dateTimeSecs;
	double rise, set;

	if ((now < 86400U) || (!gmtime_r_Custom(&now, &gmt)))
	{
		snprintf(buf, size, "SUN  no clock");
		return;
	}

	int r = sunriset((gmt.tm_year + 1900), (gmt.tm_mon + 1), gmt.tm_mday, lon, lat, (-35.0 / 60.0), 1, &rise, &set);
	if (r != 0)
	{
		snprintf(buf, size, "SUN  %s", ((r > 0) ? "always up" : "always down"));
		return;
	}

	int32_t off = localOffset();
	int32_t rs = (int32_t)(((rise * 3600.0) + off) + 86400.0) % 86400;
	int32_t ss = (int32_t)(((set * 3600.0) + off) + 86400.0) % 86400;

	snprintf(buf, size, "SUN  %02d:%02d - %02d:%02d %s", (int)(rs / 3600), (int)((rs / 60) % 60),
			(int)(ss / 3600), (int)((ss / 60) % 60), ((off != 0) ? "LT" : "UTC"));
}

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 12];
	double lat = 0.0, lon = 0.0;
	posSource_t src = getPosition(&lat, &lon);
	int16_t y;

	displayClearBuf();
	menuDisplayTitle("POSITION");

	if (src == SRC_GPS)
	{
		chip((DISPLAY_SIZE_X - 2), 2, "GPS", THEME_ITEM_FG_RSSI_BAR);
	}
	else if (src == SRC_MANUAL)
	{
		chip((DISPLAY_SIZE_X - 2), 2, "MANUAL", THEME_ITEM_FG_RSSI_BAR_S9P);
	}

	if (src == SRC_NONE)
	{
		text(4, 30, "No position", FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
		text(4, 52, "Turn the GPS on, or set", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		text(4, 62, "the location in Options.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
	}
	else
	{
		fieldUtm_t utm;

		// MGRS, big
		y = (MENU_HEADER_HEIGHT + 3);
		text(2, y, "MGRS", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		if (fieldNavToMgrs(lat, lon, buf, sizeof(buf)))
		{
			// "34U EC 00833 86586": zone + square on the first line, the digits on the second
			char *digits = strchr(strchr(buf, ' ') + 1, ' ');

			*digits++ = 0;
			text(30, (y - 1), buf, FONT_SIZE_3, THEME_ITEM_FG_RSSI_BAR, false);
			text(30, (y + 15), digits, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
		}
		else
		{
			text(30, (y - 1), "polar (UPS)", FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
		}

		// UTM
		y += 34;
		if (fieldNavToUtm(lat, lon, &utm))
		{
			snprintf(buf, sizeof(buf), "UTM %d%c %06uE %07uN", utm.zone, utm.band, (unsigned)utm.easting, (unsigned)utm.northing);
			text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
		}

		// Decimal degrees + locator
		y += 10;
		uint32_t la = (uint32_t)((fabs(lat) * 100000.0) + 0.5);
		uint32_t lo = (uint32_t)((fabs(lon) * 100000.0) + 0.5);
		snprintf(buf, sizeof(buf), "%u.%05u%c %u.%05u%c", (unsigned)(la / 100000U), (unsigned)(la % 100000U), ((lat >= 0.0) ? 'N' : 'S'),
				(unsigned)(lo / 100000U), (unsigned)(lo % 100000U), ((lon >= 0.0) ? 'E' : 'W'));
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);

		y += 10;
		char loc[12] = { 0 };
		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		snprintf(buf, sizeof(buf), "LOC %s", loc);
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ, false);

#if defined(HAS_GPS)
		if ((src == SRC_GPS) && (gpsData.Status & GPS_STATUS_HAS_HEIGHT))
		{
			snprintf(buf, sizeof(buf), "ALT %dm", (int)gpsData.HeightInM);
			text((DISPLAY_SIZE_X - 2), y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, true);
		}
#endif

		// divider
		y += 11;
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine(0, y, DISPLAY_SIZE_X, true);
		y += 3;

		// Fix quality
#if defined(HAS_GPS)
		if (src == SRC_GPS)
		{
			char hdop[8] = "-";

			if (gpsData.Status & GPS_STATUS_HAS_HDOP)
			{
				snprintf(hdop, sizeof(hdop), "%u.%u", (unsigned)(gpsData.AccuracyInCm / 100U), (unsigned)((gpsData.AccuracyInCm % 100U) / 10U));
			}
			snprintf(buf, sizeof(buf), "FIX %s  SAT %u  HDOP %s", ((gpsData.Status & GPS_STATUS_3D_FIX) ? "3D" : "2D"),
					(unsigned)(gpsData.SatsInViewGP + gpsData.SatsInViewBD), hdop);
		}
		else
#endif
		{
			snprintf(buf, sizeof(buf), "FIX none (settings)");
		}
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);

		// Sun
		y += 10;
		sunLine(lat, lon, buf, sizeof(buf));
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, false);

		// Waypoint
		y += 10;
		double wLat, wLon;
#if defined(HAS_GPS)
		if (gpsWaypointGet(&wLat, &wLon))
#else
		if (false)
#endif
		{
			double km = distanceBetweenTwoCoords(lat, lon, wLat, wLon, true);
			double brg = courseTo(lat, lon, wLat, wLon);

			if (km < 10.0)
			{
				snprintf(buf, sizeof(buf), "WPT %um  %03d %s", (unsigned)((km * 1000.0) + 0.5), (int)(brg + 0.5) % 360, fieldNavCompass(brg));
			}
			else
			{
				snprintf(buf, sizeof(buf), "WPT %ukm  %03d %s", (unsigned)(km + 0.5), (int)(brg + 0.5) % 360, fieldNavCompass(brg));
			}
			text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ, false);
		}
		else
		{
			text(2, y, "WPT none", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 2), "* MARK WAYPOINT  RED EXIT", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeResetToDefault();
	displayRender();
}

menuStatus_t menuPosition(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t lastDraw = 0U;

	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		lastDraw = ticksGetMillis();
		updateScreen();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			updateScreen();
		}
		else if (ev->events & KEY_EVENT)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
			{
				double lat, lon;

				if (getPosition(&lat, &lon) != SRC_NONE)
				{
#if defined(HAS_GPS)
					gpsWaypointSet(lat, lon);
#endif
					soundSetMelody(MELODY_ACK_BEEP);
				}
				else
				{
					soundSetMelody(MELODY_ERROR_BEEP);
				}
				updateScreen();
			}
		}
	}

	if ((ticksGetMillis() - lastDraw) >= REFRESH_MS)
	{
		lastDraw = ticksGetMillis();
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuPosition(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
