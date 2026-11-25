/*
 * Fork: net control (main menu > Net control), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Runs an EMCOM net (functions/netControl.h): GREEN starts it, then every station heard
 * on DMR (last heard list) or APRS checks in. Each row: station, check-in time (UTC),
 * time since last heard, IN (solid chip) / QUIET (outlined chip, not heard for the quiet
 * limit). FM speakers cannot be identified: '*' adds an "FM#n" check-in, GREEN on an FM#
 * row marks it heard now. GREEN on another row logs a QSO (report 59). '#' cycles the quiet
 * limit 5 / 10 / 15 / 30 min, '0' then GREEN ends the net, RED leaves (the net keeps running).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/netControl.h"
#include "user_interface/uiEmcom.h"

#if defined(HAS_NET_CONTROL)

#define ROW_H         11
#define CL_ROW_H      10
#define CL_LIST_Y     (MENU_HEADER_HEIGHT + 14)
#define FOOTER_H      11
#if defined(EMCOM_UI_MONO)
#define MONO_LIST_Y   10   // 128 x 64: 5 rows of 9 px, y 10..54
#define MONO_ROW_H    9
#define MONO_ROWS     5
#endif

typedef struct
{
	int16_t  sel;
	int16_t  top;
	bool     confirmEnd;  // '0' pressed: GREEN ends the net
	uint32_t serial;
	uint32_t lastDraw;
} netUi_t;

_Static_assert(sizeof(netUi_t) <= FIELD_SCRATCH_SIZE, "net control screen state too big");
#define ui (*(netUi_t *)fieldToolsScratch) // CCM scratch, set up on entry

static void utcTime(uint16_t sinceStartS, char *buf, size_t size)
{
	uint32_t start = netStartUtc();

	if (start == 0U)
	{
		snprintf(buf, size, "--:--");
	}
	else
	{
		time_t_custom secs = (start + sinceStartS);
		struct tm tmv;

		gmtime_r_Custom(&secs, &tmv);
		snprintf(buf, size, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
	}
}

static void runningTime(char *buf, size_t size)
{
	uint16_t s = netNowS();

	snprintf(buf, size, "%u:%02u", (unsigned)(s / 3600U), (unsigned)((s / 60U) % 60U));
}

static int visibleRows(void)
{
#if defined(HAS_EMCOM_UI)
	if (emcomStyleEnabled())
	{
#if defined(EMCOM_UI_MONO)
		return MONO_ROWS;
#else
		return ((EMCOM_UI_KEYBAR_Y - 1 - EMCOM_UI_BODY_Y) / ROW_H);
#endif
	}
#endif
	return ((DISPLAY_SIZE_Y - FOOTER_H - CL_LIST_Y) / CL_ROW_H);
}

static void clampSelection(void)
{
	int count = (int)netGetRoster()->count;
	int rows = visibleRows();

	if (ui.sel >= count)
	{
		ui.sel = (int16_t)((count > 0) ? (count - 1) : 0);
	}
	if (ui.sel < ui.top)
	{
		ui.top = ui.sel;
	}
	if (ui.sel >= (ui.top + rows))
	{
		ui.top = (int16_t)(ui.sel - rows + 1);
	}
	if (ui.top < 0)
	{
		ui.top = 0;
	}
}

static bool selectedIsFm(void)
{
	const netRoster_t *r = netGetRoster();

	return ((ui.sel < (int)r->count) && NET_KEY_IS_FM(r->e[ui.sel].key));
}

static const char *keysText(void)
{
	if (netIsRunning() == false)
	{
		return "GRN START  RED EXIT";
	}
	if (netGetRoster()->count == 0U)
	{
		return "* FM  # QUIET  0 END";
	}
	return (selectedIsFm() ? "GRN HEARD * FM # Q 0 END" : "GRN QSO  * FM  # Q  0 END");
}

#if defined(HAS_EMCOM_UI)
#if defined(EMCOM_UI_MONO)
// 128 x 64 mono (MD-9600). Title: running time and quiet limit, n/count. Rows of 9 px:
// station (bold), check-in time UTC, age, IN / QT (quiet: an inverse tag); the selected row
// as a dark bar with light text. No net / listening: a state chip with the channel and the hints.
#define MONO_TIME_X   58
#define MONO_AGE_R    109
#define MONO_TAG_X    112

static const char *keysTextMono(void)
{
	if (netIsRunning() == false)
	{
		return "GRN START  RED EXIT";
	}
	if (netGetRoster()->count == 0U)
	{
		return "* FM  # QUIET  0 END";
	}
	return (selectedIsFm() ? "GRN HRD *FM #Q 0 END" : "GRN QSO *FM #Q 0 END");
}

// IN: plain bold text; QUIET: "QT" on a block in the opposite shade of the row
static void monoStatus(int16_t rowY, bool quiet, bool sel)
{
	if (quiet)
	{
		displayFillRect(MONO_TAG_X, rowY, (DISPLAY_SIZE_X - MONO_TAG_X), MONO_ROW_H, sel); // light on the bar, else dark
		displayPrintCore((MONO_TAG_X + 2), (rowY + 1), "QT", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, (sel == false));
	}
	else
	{
		displayPrintCore((MONO_TAG_X + 2), (rowY + 1), "IN", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, sel);
	}
}

static void updateScreenMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char name[20];
	char t[8];
	const netRoster_t *r = netGetRoster();
	bool running = netIsRunning();
	uint16_t nowS = netNowS();
	uint16_t limitS = netQuietLimitS();

	displayClearBuf();
	if (running)
	{
		runningTime(t, sizeof(t));
		snprintf(name, sizeof(name), "NET %s Q%u", t, (unsigned)(limitS / 60U));
		if (r->count > 0U)
		{
			snprintf(buf, sizeof(buf), "%d/%u", (ui.sel + 1), (unsigned)r->count);
		}
		else
		{
			snprintf(buf, sizeof(buf), "0");
		}
		emcomUiTitle(name, buf);
	}
	else
	{
		emcomUiTitle("NET CONTROL", NULL);
	}

	if ((running == false) || (r->count == 0U))
	{
		// state row: OFF / RUN chip, the state word, the channel on the right
		int16_t x = (int16_t)(emcomUiChip(0, 11, (running ? "RUN" : "OFF"), THEME_ITEM_FG_DEFAULT,
				(running ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 3);

		x += (int16_t)(emcomUiText(x, 11, (running ? "LISTENING" : "NO NET"), FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false) + 6);
		netChannelName(name, sizeof(name));
		int maxChars = ((DISPLAY_SIZE_X - x) / 6);
		if (maxChars > 0)
		{
			if ((int)strlen(name) > maxChars)
			{
				name[maxChars] = 0;
			}
			emcomUiText(DISPLAY_SIZE_X, 11, name, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
		}
		emcomUiDivider(19);

		if (running == false)
		{
			emcomUiText(0, 21, "GRN starts a net:", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			emcomUiText(0, 30, "DMR / APRS stations", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			emcomUiText(0, 39, "heard check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			if (r->count > 0U)
			{
				snprintf(buf, sizeof(buf), "Last net: %u stations", (unsigned)r->count);
				emcomUiText(0, 47, buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
			}
		}
		else
		{
			emcomUiText(0, 21, "Stations heard on DMR", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			emcomUiText(0, 30, "or APRS check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			emcomUiText(0, 44, "* adds an FM station.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		}
	}
	else
	{
		int rows = visibleRows();

		for (int i = 0; i < rows; i++)
		{
			int idx = (ui.top + i);
			char call[NET_CALL_BUF];
			char age[8];
			const netEntry_t *e;
			bool sel;
			int16_t y = (int16_t)(MONO_LIST_Y + (i * MONO_ROW_H));

			if (idx >= (int)r->count)
			{
				break;
			}
			e = &r->e[idx];
			sel = (idx == ui.sel);

			if (sel)
			{
				displayFillRect(0, y, DISPLAY_SIZE_X, MONO_ROW_H, false);
			}
			netEntryCall(e, call);
			displayPrintCore(1, (y + 1), call, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, sel);
			utcTime(e->inS, t, sizeof(t));
			displayPrintCore(MONO_TIME_X, (y + 1), t, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
			netAgeText(age, sizeof(age), (uint16_t)((nowS >= e->lastS) ? (nowS - e->lastS) : 0U));
			displayPrintCore((int16_t)(MONO_AGE_R - ((int16_t)strlen(age) * 6)), (y + 1), age, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
			monoStatus(y, netEntryIsQuiet(e, nowS, limitS), sel);
		}
	}

	if (ui.confirmEnd)
	{
		emcomUiConfirmBar("END?");
	}
	else
	{
		emcomUiKeyBar(keysTextMono());
	}
	displayRender();
}
#else
// Status chip at the right of a row, 3 px from the edge: IN solid (bold text on a block),
// QUIET outlined (frame + regular text): told apart by shape as well as colour (mono / contr)
#define CHIP_RIGHT  (DISPLAY_SIZE_X - 3)
#define AGE_RIGHT   118

static void statusChip(int16_t y, bool quiet, bool sel)
{
	const char *word = (quiet ? "QUIET" : "IN");
	int16_t w = (int16_t)((strlen(word) * 6) + 4);
	int16_t x = (int16_t)(CHIP_RIGHT - w);
	themeItem_t c = (sel ? THEME_ITEM_BG : (quiet ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_RSSI_BAR));

	if (quiet)
	{
		displayThemeApply(c, THEME_ITEM_BG);
		displayDrawRect(x, (y - 1), w, 9, false);
		displayPrintCore((x + 2), y, word, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else
	{
		displayThemeApply(c, (sel ? THEME_ITEM_BG_MENU_ITEM_SELECTED : THEME_ITEM_BG));
		displayFillRect(x, (y - 1), w, 9, false);
		displayPrintCore((x + 2), y, word, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	}
	displayThemeResetToDefault();
}

static void updateScreenEmcom(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char name[20];
	const netRoster_t *r = netGetRoster();
	bool running = netIsRunning();
	uint16_t nowS = netNowS();
	uint16_t limitS = netQuietLimitS();
	int16_t y = EMCOM_UI_BODY_Y;

	displayClearBuf();
	if (running && (r->count > 0U))
	{
		snprintf(buf, sizeof(buf), "%d/%u", (ui.sel + 1), (unsigned)r->count);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)(running ? r->count : 0U));
	}
	emcomUiTitle("NET CONTROL", (running ? buf : NULL));

	// status strip: state, running time, quiet limit, channel
	emcomUiStrip();
	int16_t x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), (running ? "RUN" : "OFF"),
			(running ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION), (running ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 4);
	if (running)
	{
		runningTime(buf, sizeof(buf));
		x += (int16_t)(emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_HEADER_TEXT, false) + 5);
		snprintf(buf, sizeof(buf), "Q%u", (unsigned)(limitS / 60U));
		x += (int16_t)(emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false) + 5);
	}
	netChannelName(name, sizeof(name));
	int maxChars = ((DISPLAY_SIZE_X - 3 - x) / 6);
	if (maxChars > 0)
	{
		if ((int)strlen(name) > maxChars)
		{
			name[maxChars] = 0;
		}
		emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), name, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
	}

	if (running == false)
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((y + 10), "NO NET", FONT_SIZE_3);
		displayThemeResetToDefault();
		emcomUiText(4, (y + 34), "GREEN starts a net.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(4, (y + 44), "DMR / APRS stations", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(4, (y + 54), "heard check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		if (r->count > 0U)
		{
			snprintf(buf, sizeof(buf), "Last net: %u stations", (unsigned)r->count);
			emcomUiText(4, (y + 70), buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
		}
	}
	else if (r->count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((y + 10), "LISTENING", FONT_SIZE_3);
		displayThemeResetToDefault();
		emcomUiText(4, (y + 34), "Stations heard on DMR", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(4, (y + 44), "or APRS check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(4, (y + 58), "* adds an FM station.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
	}
	else
	{
		int rows = visibleRows();

		for (int i = 0; i < rows; i++)
		{
			int idx = (ui.top + i);
			const netEntry_t *e;
			bool sel;
			bool quiet;

			if (idx >= (int)r->count)
			{
				break;
			}
			e = &r->e[idx];
			sel = (idx == ui.sel);
			quiet = netEntryIsQuiet(e, nowS, limitS);
			y = (int16_t)(EMCOM_UI_BODY_Y + (i * ROW_H));

			if (sel)
			{
				displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
				displayFillRect(0, (y - 2), DISPLAY_SIZE_X, ROW_H, false);
				displayThemeResetToDefault();
			}

			netEntryCall(e, buf);
			emcomUiText(2, y, buf, FONT_SIZE_1_BOLD, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT), false);
			utcTime(e->inS, buf, sizeof(buf));
			emcomUiText(62, y, buf, FONT_SIZE_1, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION), false);
			netAgeText(buf, sizeof(buf), (uint16_t)((nowS >= e->lastS) ? (nowS - e->lastS) : 0U));
			emcomUiText(AGE_RIGHT, y, buf, FONT_SIZE_1, (sel ? THEME_ITEM_BG : (quiet ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_DEFAULT)), true);
			statusChip(y, quiet, sel);
		}
	}

	if (ui.confirmEnd)
	{
		emcomUiConfirmBar("END NET?");
	}
	else
	{
		emcomUiKeyBar(keysText());
	}
	displayRender();
}
#endif // EMCOM_UI_MONO
#endif

static void text(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg)
{
	displayThemeApply(fg, THEME_ITEM_BG);
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char name[20];
	const netRoster_t *r = netGetRoster();
	bool running = netIsRunning();
	uint16_t nowS = netNowS();
	uint16_t limitS = netQuietLimitS();
	int16_t y = (MENU_HEADER_HEIGHT + 2);

	clampSelection();

#if defined(HAS_EMCOM_UI)
	if (emcomStyleEnabled())
	{
#if defined(EMCOM_UI_MONO)
		updateScreenMono();
#else
		updateScreenEmcom();
#endif
		return;
	}
#endif

	displayClearBuf();
	menuDisplayTitle("NET CONTROL");

	// state in the title bar
	displayThemeApply((running ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG_MENU_NAME);
	displayFillRect((DISPLAY_SIZE_X - 22), 2, 20, 10, false);
	displayPrintCore((DISPLAY_SIZE_X - 21), 3, (running ? "RUN" : "OFF"), FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);

	// running time, quiet limit, channel
	netChannelName(name, sizeof(name));
	if (running)
	{
		char t[8];

		runningTime(t, sizeof(t));
		snprintf(buf, sizeof(buf), "%s Q%um %u/%u", t, (unsigned)(limitS / 60U), (unsigned)r->count, (unsigned)NET_ROSTER_MAX);
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);

		int maxChars = ((DISPLAY_SIZE_X - 4 - ((int)strlen(buf) * 6) - 6) / 6);
		if ((maxChars >= 0) && (maxChars < (int)strlen(name)))
		{
			name[maxChars] = 0;
		}
	}
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	displayPrintCore((DISPLAY_SIZE_X - 2 - ((int16_t)strlen(name) * 6)), y, name, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, (y + 10), DISPLAY_SIZE_X, true);

	if (running == false)
	{
		text(4, (y + 20), "No net running.", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);
		text(4, (y + 32), "GREEN starts a net:", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		text(4, (y + 42), "DMR / APRS stations", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		text(4, (y + 52), "heard check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		if (r->count > 0U)
		{
			snprintf(buf, sizeof(buf), "Last net: %u stations", (unsigned)r->count);
			text(4, (y + 68), buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);
		}
	}
	else if (r->count == 0U)
	{
		text(4, (y + 20), "Listening...", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);
		text(4, (y + 32), "Stations heard on DMR", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		text(4, (y + 42), "or APRS check in.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		text(4, (y + 56), "* adds an FM station.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
	}
	else
	{
		int rows = visibleRows();

		for (int i = 0; i < rows; i++)
		{
			int idx = (ui.top + i);
			char call[NET_CALL_BUF];
			char t[8];
			char age[8];
			const netEntry_t *e;
			bool quiet;
			int16_t ry = (int16_t)(CL_LIST_Y + (i * CL_ROW_H));

			if (idx >= (int)r->count)
			{
				break;
			}
			e = &r->e[idx];
			quiet = netEntryIsQuiet(e, nowS, limitS);

			if (idx == ui.sel)
			{
				displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
				displayFillRect(0, (ry - 1), DISPLAY_SIZE_X, CL_ROW_H, false);
			}
			netEntryCall(e, call);
			utcTime(e->inS, t, sizeof(t));
			netAgeText(age, sizeof(age), (uint16_t)((nowS >= e->lastS) ? (nowS - e->lastS) : 0U));
			snprintf(buf, sizeof(buf), "%-9s %s %3s", call, t, age);
			text(2, ry, buf, FONT_SIZE_1, THEME_ITEM_FG_MENU_ITEM);
			text((DISPLAY_SIZE_X - 3 - (quiet ? 30 : 12)), ry, (quiet ? "QUIET" : "IN"), FONT_SIZE_1_BOLD,
					(quiet ? THEME_ITEM_FG_TX_COUNTER : THEME_ITEM_FG_RSSI_BAR));
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	if (ui.confirmEnd)
	{
		displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 2), "END NET?", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(69, (fy + 2), "GREEN = yes", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 2), keysText(), FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();
	displayRender();
}

static void redraw(void)
{
	ui.serial = netSerial();
	ui.lastDraw = ticksGetMillis();
	updateScreen();
}

static void handleKeys(uiEvent_t *ev)
{
	const netRoster_t *r = netGetRoster();

	if (ui.confirmEnd)
	{
		// decided on the key release (the press of GREEN would cancel)
		if (ev->keys.event & KEY_MOD_UP)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				netEnd();
				soundSetMelody(MELODY_ACK_BEEP);
			}
			ui.confirmEnd = false;
			redraw();
		}
		return;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0); // stored as a quickkey action
		return;
	}

	if (netIsRunning() == false)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			netStart();
			ui.sel = 0;
			ui.top = 0;
			soundSetMelody(MELODY_ACK_BEEP);
			redraw();
		}
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		if ((ui.sel + 1) < (int)r->count)
		{
			ui.sel++;
			redraw();
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		if (ui.sel > 0)
		{
			ui.sel--;
			redraw();
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (ui.sel < (int)r->count))
	{
		if (NET_KEY_IS_FM(r->e[ui.sel].key))
		{
			netMarkHeard(ui.sel);
			soundSetMelody(MELODY_ACK_BEEP);
			redraw();
		}
		else
		{
			char call[NET_CALL_BUF];

			netEntryCall(&r->e[ui.sel], call);
			redraw();
			netLogQso(call);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		int index = netAddFm();

		if (index >= 0)
		{
			ui.sel = (int16_t)index;
			soundSetMelody(MELODY_ACK_BEEP);
			redraw();
		}
		else
		{
			soundSetMelody(MELODY_NACK_BEEP);
			uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, "Roster full", true);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		netQuietLimitCycle();
		redraw();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
	{
		ui.confirmEnd = true;
		redraw();
	}
}

menuStatus_t menuNetControl(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		memset(&ui, 0, sizeof(netUi_t));
		redraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			redraw();
		}
		else if (ev->events & KEY_EVENT)
		{
			handleKeys(ev);
			return MENU_STATUS_SUCCESS;
		}
	}

	// roster changes at once, the ages / running time every 5 s
	if ((ui.serial != netSerial()) || ((ticksGetMillis() - ui.lastDraw) >= 5000U))
	{
		redraw();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuNetControl(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
