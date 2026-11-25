/*
 * Fork: saved waypoints (GPS > Waypoint page > '*', quickkey "Waypoints"), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Eight named positions kept in the SPI flash (functions/fieldData.c). Each row: name,
 * distance and compass point / arrow (north up) from the GPS fix, "--" without a fix.
 *   UP / DOWN  select
 *   GREEN      navigate: the GPS waypoint target, then the GPS waypoint page (arrow)
 *   '*'        save the current GPS position in the first free slot (WP1..WP8)
 *   '#'        type a position (lat / lon keypad entry of the GPS page) into a free slot
 *   RIGHT      rename the selected waypoint (A-Z, 0-9, '-')
 *   '0'        delete the selected one (GREEN confirms)
 *   RED        back
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiTextEdit.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/fieldData.h"
#include "functions/fieldNav.h"
#include "user_interface/uiEmcom.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(HAS_FIELD_TOOLS) && defined(HAS_GPS) && defined(HAS_EMCOM_UI) && !defined(PLATFORM_MD9600) // MD-9600: needs the GPS waypoint page

#define REFRESH_MS   1000U
#define ROW_H        11
#define CL_LIST_Y    (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H     11
#define NAME_X       10
#define DIST_R       118  // distance, right aligned
#define COMPASS_R    142  // compass point, right aligned
#define ARROW_CX     151

typedef enum
{
	WP_MODE_LIST = 0,
	WP_MODE_CONFIRM_DELETE,
	WP_MODE_RENAME,
	WP_MODE_KEYPAD
} wpMode_t;

typedef struct
{
	uint8_t  mode;
	char     name[FIELD_WAYPOINT_NAME_LEN + 1]; // rename buffer
	uint32_t lastDraw;
} wpUi_t;

_Static_assert(sizeof(wpUi_t) <= FIELD_SCRATCH_SIZE, "waypoint screen state too big");
#define ui (*(wpUi_t *)fieldToolsScratch) // CCM scratch, set up on entry

static bool haveFix(double *lat, double *lon)
{
	if ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) &&
			((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return true;
	}
	return false;
}

static int selected(void)
{
	return (int)menuDataGlobal.currentItemIndex;
}

static bool isTarget(const fieldWaypoint_t *wp)
{
	double lat, lon;

	return (gpsWaypointGet(&lat, &lon) && (fabs((lat * 1e6) - wp->lat) < 1.0) && (fabs((lon * 1e6) - wp->lon) < 1.0));
}

static void formatDistance(int meters, char *buf, size_t size)
{
	if (meters < 1000)
	{
		snprintf(buf, size, "%dm", meters);
	}
	else if (meters < 100000)
	{
		snprintf(buf, size, "%d.%01dkm", (meters / 1000), ((meters % 1000) / 100));
	}
	else
	{
		snprintf(buf, size, "%dkm", (meters / 1000));
	}
}

// Small arrow (north up) at the end of a row: tip and wings 5 px from the centre, wings at +-150 deg
static void drawArrow(int16_t cy, double bearing, themeItem_t colour)
{
	double a = bearing * DEG_TO_RAD;
	double aL = (bearing + 150.0) * DEG_TO_RAD;
	double aR = (bearing - 150.0) * DEG_TO_RAD;
	int16_t tipX = ARROW_CX + (int16_t)lround(sin(a) * 5.0);
	int16_t tipY = cy - (int16_t)lround(cos(a) * 5.0);
	int16_t lX = ARROW_CX + (int16_t)lround(sin(aL) * 5.0);
	int16_t lY = cy - (int16_t)lround(cos(aL) * 5.0);
	int16_t rX = ARROW_CX + (int16_t)lround(sin(aR) * 5.0);
	int16_t rY = cy - (int16_t)lround(cos(aR) * 5.0);

	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillTriangle(tipX, tipY, lX, lY, ARROW_CX, cy, true);
	displayFillTriangle(tipX, tipY, rX, rY, ARROW_CX, cy, true);
	displayThemeResetToDefault();
}

static void drawRow(int i, int16_t y, bool fix, double lat, double lon, int *used)
{
	fieldWaypoint_t wp;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	bool sel = (i == selected());
	themeItem_t text = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT);
	themeItem_t muted = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION);

	if (sel)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRect(0, y, DISPLAY_SIZE_X, (ROW_H - 1), false);
		displayThemeResetToDefault();
	}

	if (fieldWaypointGet(i, &wp) == false)
	{
		snprintf(buf, sizeof(buf), "%d", (i + 1));
		emcomUiText(2, (y + 1), buf, FONT_SIZE_1, muted, false);
		emcomUiText(NAME_X, (y + 1), "-- empty", FONT_SIZE_1, muted, false);
		return;
	}

	(*used)++;
	bool target = isTarget(&wp);

	// '>' marks the navigation target (shape, not only colour)
	snprintf(buf, sizeof(buf), "%c", (target ? '>' : (char)('1' + i)));
	emcomUiText(2, (y + 1), buf, FONT_SIZE_1, (target ? (sel ? THEME_ITEM_BG : THEME_ITEM_FG_RX_FREQ) : muted), false);
	fieldWaypointName(&wp, buf);
	emcomUiText(NAME_X, (y + 1), buf, FONT_SIZE_1_BOLD, (target && (sel == false) ? THEME_ITEM_FG_RX_FREQ : text), false);

	if (fix == false)
	{
		emcomUiText(DIST_R, (y + 1), "--", FONT_SIZE_1, muted, true);
		return;
	}

	double wLat = (wp.lat / 1e6), wLon = (wp.lon / 1e6);
	int meters = (int)distanceBetweenTwoCoords(lat, lon, wLat, wLon, false);
	double bearing = courseTo(lat, lon, wLat, wLon);

	formatDistance(meters, buf, sizeof(buf));
	emcomUiText(DIST_R, (y + 1), buf, FONT_SIZE_1, text, true);
	if (meters >= 5)
	{
		emcomUiText(COMPASS_R, (y + 1), fieldNavCompass(bearing), FONT_SIZE_1, muted, true);
		drawArrow((int16_t)(y + 5), bearing, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_RSSI_BAR));
	}
	else
	{
		emcomUiText(COMPASS_R + 15, (y + 1), "HERE", FONT_SIZE_1, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_RSSI_BAR), true);
	}
}

static void keyText(char *buf, size_t size, bool selUsed)
{
	if (ui.mode == WP_MODE_CONFIRM_DELETE)
	{
		snprintf(buf, size, "DELETE %d?", (selected() + 1));
	}
	else
	{
		snprintf(buf, size, "%s", (selUsed ? "GRN GO  RGT NAME  0 DEL" : "* SAVE HERE  # LAT/LON"));
	}
}

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	fieldWaypoint_t wp;
	double lat = 0.0, lon = 0.0;
	bool fix = haveFix(&lat, &lon);
	bool emcom = emcomStyleEnabled();
	bool selUsed = fieldWaypointGet(selected(), &wp);
	int used = 0;

	ui.lastDraw = ticksGetMillis();
	displayClearBuf();

	if (ui.mode == WP_MODE_RENAME)
	{
		snprintf(buf, sizeof(buf), "SLOT %d", (selected() + 1));
		uiTextEditDraw("WAYPOINT NAME", buf);
		displayRender();
		return;
	}

	if (ui.mode == WP_MODE_KEYPAD)
	{
		if (emcom)
		{
			emcomUiTitle("NEW WAYPOINT", NULL);
		}
		else
		{
			menuDisplayTitle("New waypoint");
		}
		gpsWaypointEditDraw();
		displayRender();
		return;
	}

	if (emcom)
	{
		snprintf(buf, sizeof(buf), "%d/%d", (selected() + 1), FIELD_WAYPOINT_SLOTS);
		emcomUiTitle("WAYPOINTS", buf);
	}
	else
	{
		menuDisplayTitle("Waypoints");
	}

	int16_t y0 = (emcom ? EMCOM_UI_BODY_Y : CL_LIST_Y);

	for (int i = 0; i < FIELD_WAYPOINT_SLOTS; i++)
	{
		drawRow(i, (int16_t)(y0 + (i * ROW_H)), fix, lat, lon, &used);
	}

	if (emcom)
	{
		// status strip: fix state, slots used
		emcomUiStrip();
		emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), (fix ? "FIX" : "NO FIX"), (fix ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RSSI_BAR_S9P),
				(fix ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false);
		snprintf(buf, sizeof(buf), "%d/%d SAVED", used, FIELD_WAYPOINT_SLOTS);
		emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);

		keyText(buf, sizeof(buf), selUsed);
		if (ui.mode == WP_MODE_CONFIRM_DELETE)
		{
			emcomUiConfirmBar(buf);
		}
		else
		{
			emcomUiKeyBar(buf);
		}
	}
	else
	{
		int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);

		displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
		displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
		keyText(buf, sizeof(buf), selUsed);
		if (ui.mode == WP_MODE_CONFIRM_DELETE)
		{
			displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(3, (fy + 2), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
			displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(75, (fy + 2), "GREEN = yes", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
		else
		{
			displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(3, (fy + 2), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
		displayThemeResetToDefault();
	}

	displayRender();
}

static void saved(int slot)
{
	char buf[24];

	if (slot < 0)
	{
		soundSetMelody(MELODY_ERROR_BEEP);
		uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2000U, "All 8 used\n0 deletes one", false);
		return;
	}

	menuDataGlobal.currentItemIndex = slot;
	snprintf(buf, sizeof(buf), "Saved WP%d", (slot + 1));
	soundSetMelody(MELODY_ACK_BEEP);
	uiNotificationShowKind(UI_ALERT_INFO, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, buf, false);
}

static void handleKeypad(uiEvent_t *ev)
{
	double lat, lon;
	gpsWaypointEdit_t r = gpsWaypointEditHandleEvent(ev, &lat, &lon);

	if (r == GPS_WP_EDIT_NONE)
	{
		return;
	}

	if ((r == GPS_WP_EDIT_DONE) || (r == GPS_WP_EDIT_CANCEL))
	{
		ui.mode = WP_MODE_LIST;
		if (r == GPS_WP_EDIT_DONE)
		{
			saved(fieldWaypointAdd(lat, lon, NULL, FIELD_WAYPOINT_FLAG_KEYPAD));
		}
	}
	updateScreen();
}

static void handleRename(uiEvent_t *ev)
{
	textEditResult_t r = uiTextEditHandleEvent(ev);

	if (r == TEXT_EDIT_NONE)
	{
		return;
	}

	if (r == TEXT_EDIT_DONE)
	{
		fieldWaypoint_t wp;

		if ((ui.name[0] != 0) && fieldWaypointGet(selected(), &wp))
		{
			memset(wp.name, 0, sizeof(wp.name));
			memcpy(wp.name, ui.name, strlen(ui.name));
			fieldWaypointSet(selected(), &wp);
			soundSetMelody(MELODY_ACK_BEEP);
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
	}

	if ((r == TEXT_EDIT_DONE) || (r == TEXT_EDIT_CANCEL))
	{
		uiTextEditStop();
		ui.mode = WP_MODE_LIST;
	}
	updateScreen();
}

static void handleList(uiEvent_t *ev)
{
	fieldWaypoint_t wp;
	double lat, lon;

	if (ui.mode == WP_MODE_CONFIRM_DELETE)
	{
		// decided on the key release (the press of GREEN would cancel); RED cancels, stays here
		if (ev->keys.event & KEY_MOD_UP)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				fieldWaypointSet(selected(), NULL);
				soundSetMelody(MELODY_ACK_BEEP);
			}
			ui.mode = WP_MODE_LIST;
			updateScreen();
		}
		return;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (fieldWaypointGet(selected(), &wp))
		{
			char buf[24];
			char name[FIELD_WAYPOINT_NAME_LEN + 1];

			gpsWaypointSet((wp.lat / 1e6), (wp.lon / 1e6));
			fieldWaypointName(&wp, name);
			snprintf(buf, sizeof(buf), "Target %s", name);
			soundSetMelody(MELODY_ACK_BEEP);
			uiNotificationShowKind(UI_ALERT_INFO, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, buf, false);
			menuSystemPopPreviousMenu();
			gpsShowWaypointPage();
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		if (haveFix(&lat, &lon))
		{
			saved(fieldWaypointAdd(lat, lon, NULL, FIELD_WAYPOINT_FLAG_GPS));
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
			uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, "No GPS fix", false);
		}
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		gpsWaypointEditStart(false);
		ui.mode = WP_MODE_KEYPAD;
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		if (fieldWaypointGet(selected(), &wp))
		{
			fieldWaypointName(&wp, ui.name);
			uiTextEditStart(ui.name, FIELD_WAYPOINT_NAME_LEN, true);
			ui.mode = WP_MODE_RENAME;
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
	{
		if (fieldWaypointGet(selected(), &wp))
		{
			ui.mode = WP_MODE_CONFIRM_DELETE;
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP); // already empty
		}
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuDataGlobal.currentItemIndex = ((selected() + 1) % FIELD_WAYPOINT_SLOTS);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuDataGlobal.currentItemIndex = ((selected() + FIELD_WAYPOINT_SLOTS - 1) % FIELD_WAYPOINT_SLOTS);
		updateScreen();
	}
}

menuStatus_t menuWaypoints(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		memset(&ui, 0, sizeof(ui));
		menuDataGlobal.numItems = FIELD_WAYPOINT_SLOTS;
		if ((menuDataGlobal.currentItemIndex < 0) || (menuDataGlobal.currentItemIndex >= FIELD_WAYPOINT_SLOTS))
		{
			menuDataGlobal.currentItemIndex = 0;
		}
		gpsWaypointEditStop();
		uiTextEditStop();
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
			switch (ui.mode)
			{
				case WP_MODE_KEYPAD:
					handleKeypad(ev);
					break;
				case WP_MODE_RENAME:
					handleRename(ev);
					break;
				default:
					handleList(ev);
					break;
			}
			return MENU_STATUS_SUCCESS;
		}
	}

	// distances follow the GPS fix
	if ((ui.mode <= WP_MODE_CONFIRM_DELETE) && ((ticksGetMillis() - ui.lastDraw) >= REFRESH_MS))
	{
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuWaypoints(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
