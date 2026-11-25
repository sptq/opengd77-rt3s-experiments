/*
 * Fork: comms schedule (PACE > '#', quickkey "Sked"), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Four skeds (functions/sked.h). List: UP / DOWN select, GREEN edits (an empty slot starts
 * as "every hour at :00, 10 min"), '0' then GREEN deletes, RED leaves.
 * Edit: UP / DOWN choose the field, LEFT / RIGHT change it, GREEN saves, RED cancels.
 *   EVERY  15 / 20 / 30 min, 1 / 2 / 3 / 4 / 6 / 8 / 12 h, day
 *   AT     the start: minute of the hour (5 min steps), or time of day UTC (15 min steps)
 *   LENGTH 1..60 min
 *   TUNE   none, PACE P / A / C / E, or the current channel (channel mode)
 *   ALERT  on / off
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/codeplug.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/sked.h"
#include "user_interface/uiEmcom.h"

#if defined(HAS_SKED) && defined(HAS_EMCOM_UI)

#define CL_LIST_Y    (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H     11
#define CHIP_W       20
#define TEXT_X       (CHIP_W + 4)
#define EDIT_ROW_H   15
#define EDIT_VALUE_X 58

typedef enum
{
	SK_MODE_LIST = 0,
	SK_MODE_CONFIRM_DELETE,
	SK_MODE_EDIT
} skMode_t;

typedef enum
{
	SK_FIELD_PERIOD = 0,
	SK_FIELD_START,
	SK_FIELD_LENGTH,
	SK_FIELD_TUNE,
	SK_FIELD_ALERT,
	SK_FIELD_COUNT
} skField_t;

typedef struct
{
	uint8_t     mode;
	uint8_t     field;
	uint32_t    drawnSecs;
	fieldSked_t edit;
} skUi_t;

_Static_assert(sizeof(skUi_t) <= FIELD_SCRATCH_SIZE, "sked screen state too big");
#define ui (*(skUi_t *)fieldToolsScratch) // CCM scratch, set up on entry

static const char paceLetters[PACE_SLOTS] = { 'P', 'A', 'C', 'E' };
static const char *const fieldNames[SK_FIELD_COUNT] = { "EVERY", "AT", "LENGTH", "TUNE", "ALERT" };

static int selected(void)
{
	return (int)menuDataGlobal.currentItemIndex;
}

static bool onChannelScreen(void)
{
	return (menuDataGlobal.controlData.stack[0] == UI_CHANNEL_MODE);
}

static void periodText(uint16_t p, char *buf, size_t size)
{
	if (p >= SKED_DAY_MIN)
	{
		snprintf(buf, size, "DAY");
	}
	else if (p < 60U)
	{
		snprintf(buf, size, "%u MIN", (unsigned)p);
	}
	else
	{
		snprintf(buf, size, "%u H", (unsigned)(p / 60U));
	}
}

static void startText(const skedTime_t *t, char *buf, size_t size)
{
	if (t->periodMin <= 60U)
	{
		snprintf(buf, size, ":%02u", (unsigned)t->offsetMin);
	}
	else
	{
		snprintf(buf, size, "%02u:%02u UTC", (unsigned)(t->offsetMin / 60U), (unsigned)(t->offsetMin % 60U));
	}
}

static void tuneText(const fieldSked_t *s, char *buf, size_t size)
{
	if ((s->time.tune >= SKED_TUNE_PACE_FIRST) && (s->time.tune < (SKED_TUNE_PACE_FIRST + PACE_SLOTS)))
	{
		snprintf(buf, size, "PACE %c", paceLetters[s->time.tune - SKED_TUNE_PACE_FIRST]);
	}
	else if (s->time.tune == SKED_TUNE_CHANNEL)
	{
		char name[24];

		skedTargetName(s, name, sizeof(name));
		snprintf(buf, size, "CH %s", name);
	}
	else
	{
		snprintf(buf, size, "NONE");
	}
}

// --- list ------------------------------------------------------------------------------------
#define EM_ROW_H    ((EMCOM_UI_KEYBAR_Y - EMCOM_UI_BODY_Y + 1) / FIELD_SKED_SLOTS)
#define CL_ROW_H    ((DISPLAY_SIZE_Y - CL_LIST_Y - FOOTER_H - 1) / FIELD_SKED_SLOTS)

static void drawListRow(int i, int16_t y, int16_t rowH)
{
	fieldSked_t s;
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char cd[12];
	bool sel = (i == selected());
	bool used = fieldSkedGet(i, &s);
	bool clock = skedClockValid();
	bool on = (used && s.time.enabled && clock && skedIsOn(uiDataGlobal.dateTimeSecs, &s.time));
	themeItem_t colour = (on ? THEME_ITEM_FG_RSSI_BAR : ((used && s.time.enabled) ? THEME_ITEM_FG_RX_FREQ : THEME_ITEM_FG_DECORATION));
	themeItem_t text = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT);
	themeItem_t muted = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION);

	// slot block "S1", the selected row as a bar beside it
	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillRect(0, y, CHIP_W, (rowH - 1), false);
	snprintf(buf, sizeof(buf), "S%d", (i + 1));
	displayPrintCore(((CHIP_W - 12) / 2), (y + ((rowH - 1 - 8) / 2)), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	if (sel)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRect((CHIP_W + 1), y, (DISPLAY_SIZE_X - CHIP_W - 1), (rowH - 1), false);
	}
	else if (i < (FIELD_SKED_SLOTS - 1))
	{
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine((CHIP_W + 1), (y + rowH - 1), (DISPLAY_SIZE_X - CHIP_W - 1), true);
	}
	displayThemeResetToDefault();

	if (used == false)
	{
		emcomUiText(TEXT_X, (y + 3), "NO SKED", FONT_SIZE_1_BOLD, muted, false);
		emcomUiText(TEXT_X, (y + 12), "GREEN sets one", FONT_SIZE_1, muted, false);
		emcomUiChip((DISPLAY_SIZE_X - 2), (y + 3), "EMPTY", (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION), EMCOM_CHIP_OUTLINE, true);
		return;
	}

	skedFormatWhen(&s.time, buf, sizeof(buf));
	emcomUiText(TEXT_X, (y + 3), buf, FONT_SIZE_1_BOLD, text, false);

	// state chip: NOW (solid), ON / OFF (outlined)
	if (on)
	{
		emcomUiChip((DISPLAY_SIZE_X - 2), (y + 3), "NOW", (sel ? THEME_ITEM_BG : THEME_ITEM_FG_RSSI_BAR), (sel ? EMCOM_CHIP_OUTLINE : EMCOM_CHIP_SOLID), true);
	}
	else
	{
		emcomUiChip((DISPLAY_SIZE_X - 2), (y + 3), (s.time.enabled ? "ON" : "OFF"), (sel ? THEME_ITEM_BG : colour), EMCOM_CHIP_OUTLINE, true);
	}

	// second line: length, tune, count-down
	char tune[24];

	tuneText(&s, tune, sizeof(tune));
	snprintf(buf, sizeof(buf), "%uM %s", (unsigned)s.time.durationMin, tune);
	if (s.time.enabled && clock)
	{
		skedFormatCountdown((on ? skedSecondsSinceStart(uiDataGlobal.dateTimeSecs, &s.time) : skedSecondsToStart(uiDataGlobal.dateTimeSecs, &s.time)), cd, sizeof(cd));
		int16_t w = emcomUiText((DISPLAY_SIZE_X - 2), (y + 12), cd, FONT_SIZE_1, (sel ? THEME_ITEM_BG : (on ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RSSI_BAR_S9P)), true);
		int maxChars = ((DISPLAY_SIZE_X - 2 - w - 6 - TEXT_X) / 6); // a char of space before the count-down

		if ((int)strlen(buf) > maxChars)
		{
			buf[(maxChars > 0) ? maxChars : 0] = 0;
		}
	}
	emcomUiText(TEXT_X, (y + 12), buf, FONT_SIZE_1, muted, false);
}

static void drawStrip(void)
{
	skedNext_t next;
	char buf[24];
	char cd[12];
	char utc[8];

	emcomUiStrip();
	skedNextGet(&next);

	if (skedClockValid() == false)
	{
		emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), "NO CLOCK", THEME_ITEM_FG_TX_COUNTER, EMCOM_CHIP_OUTLINE, false);
		return;
	}

	if (next.slot < 0)
	{
		emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), "NO SKED", THEME_ITEM_FG_DECORATION, EMCOM_CHIP_OUTLINE, false);
	}
	else
	{
		snprintf(buf, sizeof(buf), (next.on ? "S%d NOW" : "NEXT S%d"), (next.slot + 1));
		int16_t x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), buf, (next.on ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RSSI_BAR_S9P),
				(next.on ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 4);
		skedFormatCountdown(next.secs, cd, sizeof(cd));
		emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), cd, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
	}

	uint32_t daySecs = (uiDataGlobal.dateTimeSecs % 86400U);
	snprintf(utc, sizeof(utc), "%02u:%02u", (unsigned)(daySecs / 3600U), (unsigned)((daySecs / 60U) % 60U));
	snprintf(buf, sizeof(buf), "UTC %s", utc);
	emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
}

// --- editor ----------------------------------------------------------------------------------
static void fieldValue(skField_t f, char *buf, size_t size)
{
	switch (f)
	{
		case SK_FIELD_PERIOD: periodText(ui.edit.time.periodMin, buf, size); break;
		case SK_FIELD_START:  startText(&ui.edit.time, buf, size); break;
		case SK_FIELD_LENGTH: snprintf(buf, size, "%u MIN", (unsigned)ui.edit.time.durationMin); break;
		case SK_FIELD_TUNE:   tuneText(&ui.edit, buf, size); break;
		default:              snprintf(buf, size, "%s", (ui.edit.time.enabled ? "ON" : "OFF")); break;
	}
}

static void drawEditor(int16_t y0, bool emcom)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];

	for (int f = 0; f < SK_FIELD_COUNT; f++)
	{
		int16_t y = (int16_t)(y0 + (f * EDIT_ROW_H));
		bool sel = (f == ui.field);

		if (sel)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect(0, y, DISPLAY_SIZE_X, (EDIT_ROW_H - 2), false);
			displayThemeResetToDefault();
		}
		emcomUiText(4, (y + 3), fieldNames[f], FONT_SIZE_1, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION), false);
		fieldValue((skField_t)f, buf, sizeof(buf));
		buf[(DISPLAY_SIZE_X - EDIT_VALUE_X - 14) / 6] = 0;
		emcomUiText(EDIT_VALUE_X, (y + 3), buf, FONT_SIZE_1_BOLD, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT), false);
		if (sel)
		{
			// < > : LEFT / RIGHT change it
			emcomUiText((EDIT_VALUE_X - 8), (y + 3), "<", FONT_SIZE_1_BOLD, THEME_ITEM_BG, false);
			emcomUiText((DISPLAY_SIZE_X - 3), (y + 3), ">", FONT_SIZE_1_BOLD, THEME_ITEM_BG, true);
		}
	}

	// summary under the fields
	skedFormatWhen(&ui.edit.time, buf, sizeof(buf));
	snprintf(&buf[strlen(buf)], (sizeof(buf) - strlen(buf)), " %uM", (unsigned)ui.edit.time.durationMin);
	emcomUiText(4, (int16_t)(y0 + (SK_FIELD_COUNT * EDIT_ROW_H) + (emcom ? 1 : 0)), buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, false);
}

#if defined(EMCOM_UI_MONO)
// --- 128 x 64 mono (MD-9600) -------------------------------------------------------------------
// List: title "SKED" with the next sked / NOW and its count-down (or NO SKED / NO CLOCK), 4 slot
// rows of 9 px (slot, period and start, count-down, NOW / ON / OFF / EMPTY tag), a divider and the
// selected slot's length and tune with the UTC clock. Editor: the 5 fields as rows of 9 px.
#define MONO_LIST_Y    10
#define MONO_ROW_H     9
#define MONO_WHEN_X    16
#define MONO_CD_R      104
#define MONO_DETAIL_Y  47
#define MONO_VALUE_X   52

// skedFormatWhen() in at most 9 characters: "DAY 06:00", "1H :00", "30M :15", "12H 01:30"
static void whenShort(const skedTime_t *t, char *buf, size_t size)
{
	unsigned h = (unsigned)(t->offsetMin / 60U), m = (unsigned)(t->offsetMin % 60U);

	if (t->periodMin >= SKED_DAY_MIN)
	{
		snprintf(buf, size, "DAY %02u:%02u", h, m);
	}
	else if (t->periodMin == 60U)
	{
		snprintf(buf, size, "1H :%02u", m);
	}
	else if (t->periodMin < 60U)
	{
		snprintf(buf, size, "%uM :%02u", (unsigned)t->periodMin, m);
	}
	else
	{
		snprintf(buf, size, "%uH %02u:%02u", (unsigned)(t->periodMin / 60U), h, m);
	}
}

// State tag ending at xr: solid (inverse block, bold) or outlined; the shades swap on the selected bar
static void monoTag(int16_t xr, int16_t y, const char *str, bool solid, bool sel)
{
	int16_t w = (int16_t)((strlen(str) * 6) + 3);
	int16_t x = (int16_t)(xr - w);

	if (solid)
	{
		displayFillRect(x, y, w, MONO_ROW_H, sel);
		displayPrintCore((x + 2), (y + 1), str, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, (sel == false));
	}
	else
	{
		displayDrawRect(x, y, w, MONO_ROW_H, (sel == false));
		displayPrintCore((x + 2), (y + 1), str, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
	}
}

static void drawListRowMono(int i, int16_t y)
{
	fieldSked_t s;
	char buf[16];
	char cd[12];
	bool sel = (i == selected());
	bool used = fieldSkedGet(i, &s);
	bool clock = skedClockValid();
	bool on = (used && s.time.enabled && clock && skedIsOn(uiDataGlobal.dateTimeSecs, &s.time));

	if (sel)
	{
		displayFillRect(0, y, DISPLAY_SIZE_X, MONO_ROW_H, false);
	}
	snprintf(buf, sizeof(buf), "S%d", (i + 1));
	displayPrintCore(1, (y + 1), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, sel);

	if (used == false)
	{
		displayPrintCore(MONO_WHEN_X, (y + 1), "NO SKED", FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
		monoTag(DISPLAY_SIZE_X, y, "EMPTY", false, sel);
		return;
	}

	whenShort(&s.time, buf, sizeof(buf));
	displayPrintCore(MONO_WHEN_X, (y + 1), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, sel);
	monoTag(DISPLAY_SIZE_X, y, (on ? "NOW" : (s.time.enabled ? "ON" : "OFF")), on, sel);
	if (s.time.enabled && clock)
	{
		skedFormatCountdown((on ? skedSecondsSinceStart(uiDataGlobal.dateTimeSecs, &s.time) : skedSecondsToStart(uiDataGlobal.dateTimeSecs, &s.time)), cd, sizeof(cd));
		displayPrintCore((int16_t)(MONO_CD_R - ((int16_t)strlen(cd) * 6)), (y + 1), cd, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
	}
}

// Selected slot: length and tune (or how to set one), the UTC clock on the right
static void drawDetailMono(void)
{
	fieldSked_t s;
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char tune[24];
	int16_t rw = 0;

	emcomUiDivider(MONO_DETAIL_Y - 1);
	if (skedClockValid())
	{
		uint32_t daySecs = (uiDataGlobal.dateTimeSecs % 86400U);

		snprintf(buf, sizeof(buf), "%02u:%02uZ", (unsigned)(daySecs / 3600U), (unsigned)((daySecs / 60U) % 60U));
		rw = (int16_t)(emcomUiText(DISPLAY_SIZE_X, MONO_DETAIL_Y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true) + 6);
	}

	if (fieldSkedGet(selected(), &s))
	{
		tuneText(&s, tune, sizeof(tune));
		snprintf(buf, sizeof(buf), "%uM %s", (unsigned)s.time.durationMin, tune);
	}
	else
	{
		snprintf(buf, sizeof(buf), "GRN sets one");
	}
	buf[(DISPLAY_SIZE_X - rw) / 6] = 0;
	emcomUiText(0, MONO_DETAIL_Y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
}

static void drawEditorMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];

	for (int f = 0; f < SK_FIELD_COUNT; f++)
	{
		int16_t y = (int16_t)(MONO_LIST_Y + (f * MONO_ROW_H));
		bool sel = (f == ui.field);

		if (sel)
		{
			displayFillRect(0, y, DISPLAY_SIZE_X, MONO_ROW_H, false);
		}
		displayPrintCore(2, (y + 1), fieldNames[f], FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
		fieldValue((skField_t)f, buf, sizeof(buf));
		buf[(DISPLAY_SIZE_X - MONO_VALUE_X - 8) / 6] = 0;
		displayPrintCore(MONO_VALUE_X, (y + 1), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, sel);
		if (sel)
		{
			// < > : LEFT / RIGHT change it
			displayPrintCore((MONO_VALUE_X - 8), (y + 1), "<", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
			displayPrintCore((DISPLAY_SIZE_X - 6), (y + 1), ">", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
		}
	}
}

static void updateScreenMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];

	displayClearBuf();

	if (ui.mode == SK_MODE_EDIT)
	{
		snprintf(buf, sizeof(buf), "S%d", (selected() + 1));
		emcomUiTitle("EDIT SKED", buf);
		drawEditorMono();
		emcomUiKeyBar("UP/DN <> SET GRN SAVE");
	}
	else
	{
		skedNext_t next;
		char cd[12];

		// the next sked (or the one on now) with its count-down, in the title bar
		skedNextGet(&next);
		if (skedClockValid() == false)
		{
			snprintf(buf, sizeof(buf), "NO CLOCK");
		}
		else if (next.slot < 0)
		{
			snprintf(buf, sizeof(buf), "NO SKED");
		}
		else
		{
			skedFormatCountdown(next.secs, cd, sizeof(cd));
			snprintf(buf, sizeof(buf), (next.on ? "S%d NOW %s" : "NEXT S%d %s"), (next.slot + 1), cd);
		}
		emcomUiTitle("SKED", buf);

		for (int i = 0; i < FIELD_SKED_SLOTS; i++)
		{
			drawListRowMono(i, (int16_t)(MONO_LIST_Y + (i * MONO_ROW_H)));
		}
		drawDetailMono();

		if (ui.mode == SK_MODE_CONFIRM_DELETE)
		{
			snprintf(buf, sizeof(buf), "DEL S%d", (selected() + 1));
			emcomUiConfirmBar(buf);
		}
		else
		{
			emcomUiKeyBar("GRN EDIT 0 DEL RED BK");
		}
	}

	displayRender();
}
#endif

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	bool emcom = emcomStyleEnabled();

	ui.drawnSecs = uiDataGlobal.dateTimeSecs;
#if defined(EMCOM_UI_MONO)
	if (emcom)
	{
		updateScreenMono();
		return;
	}
#endif
	displayClearBuf();

	if (emcom)
	{
		snprintf(buf, sizeof(buf), "%d/%d", (selected() + 1), FIELD_SKED_SLOTS);
		if (ui.mode == SK_MODE_EDIT)
		{
			snprintf(buf, sizeof(buf), "S%d", (selected() + 1));
			emcomUiTitle("EDIT SKED", buf);
		}
		else
		{
			emcomUiTitle("SKED", buf);
		}
		drawStrip();
	}
	else
	{
		if (ui.mode == SK_MODE_EDIT)
		{
			snprintf(buf, sizeof(buf), "Sked %d", (selected() + 1));
			menuDisplayTitle(buf);
		}
		else
		{
			menuDisplayTitle("Sked");
		}
	}

	if (ui.mode == SK_MODE_EDIT)
	{
		drawEditor((emcom ? EMCOM_UI_BODY_Y : CL_LIST_Y), emcom);
	}
	else
	{
		for (int i = 0; i < FIELD_SKED_SLOTS; i++)
		{
			if (emcom)
			{
				drawListRow(i, (int16_t)(EMCOM_UI_BODY_Y + (i * EM_ROW_H)), EM_ROW_H);
			}
			else
			{
				drawListRow(i, (int16_t)(CL_LIST_Y + (i * CL_ROW_H)), CL_ROW_H);
			}
		}
	}

	const char *keys = ((ui.mode == SK_MODE_EDIT) ? "UP/DN  < > SET  GRN SAVE" : "GRN EDIT  0 DEL  RED BACK");

	if (ui.mode == SK_MODE_CONFIRM_DELETE)
	{
		snprintf(buf, sizeof(buf), "DELETE S%d?", (selected() + 1));
	}

	if (emcom)
	{
		if (ui.mode == SK_MODE_CONFIRM_DELETE)
		{
			emcomUiConfirmBar(buf);
		}
		else
		{
			emcomUiKeyBar(keys);
		}
	}
	else
	{
		int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);

		displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
		displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
		if (ui.mode == SK_MODE_CONFIRM_DELETE)
		{
			displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(3, (fy + 2), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
			displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(75, (fy + 2), "GREEN = yes", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
		else
		{
			displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
			displayPrintCore(3, (fy + 2), keys, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
		displayThemeResetToDefault();
	}

	displayRender();
}

static void editChange(int dir)
{
	skedTime_t *t = &ui.edit.time;

	switch (ui.field)
	{
		case SK_FIELD_PERIOD:
			t->periodMin = skedPeriodStep(t->periodMin, dir);
			skedTimeFix(t);
			break;
		case SK_FIELD_START:
			t->offsetMin = skedOffsetStep(t->periodMin, t->offsetMin, dir);
			break;
		case SK_FIELD_LENGTH:
			t->durationMin = skedDurationStep(t->periodMin, t->durationMin, dir);
			break;
		case SK_FIELD_TUNE:
			do
			{
				t->tune = (uint8_t)((t->tune + SKED_TUNE_MAX + ((dir > 0) ? 1 : -1)) % SKED_TUNE_MAX);
			} while ((t->tune == SKED_TUNE_CHANNEL) && (onChannelScreen() == false)); // VFO: no channel to store

			if (t->tune == SKED_TUNE_CHANNEL)
			{
				ui.edit.channel.zoneNumber = nonVolatileSettings.currentZone;
				ui.edit.channel.channelNumber = (int16_t)uiDataGlobal.currentSelectedChannelNumber;
			}
			break;
		default:
			t->enabled = (t->enabled ? 0U : 1U);
			break;
	}
}

static void handleEdit(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		ui.mode = SK_MODE_LIST; // cancel
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (skedTimeIsValid(&ui.edit.time))
		{
			fieldSkedSet(selected(), &ui.edit);
			soundSetMelody(MELODY_ACK_BEEP);
			ui.mode = SK_MODE_LIST;
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		ui.field = (uint8_t)((ui.field + 1) % SK_FIELD_COUNT);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		ui.field = (uint8_t)((ui.field + SK_FIELD_COUNT - 1) % SK_FIELD_COUNT);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RIGHT))
	{
		editChange(1);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_LEFT))
	{
		editChange(-1);
		updateScreen();
	}
}

static void handleList(uiEvent_t *ev)
{
	fieldSked_t s;

	if (ui.mode == SK_MODE_CONFIRM_DELETE)
	{
		// decided on the key release (the press of GREEN would cancel); RED cancels, stays here
		if (ev->keys.event & KEY_MOD_UP)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				fieldSkedSet(selected(), NULL);
				soundSetMelody(MELODY_ACK_BEEP);
			}
			ui.mode = SK_MODE_LIST;
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
		if (fieldSkedGet(selected(), &ui.edit) == false)
		{
			memset(&ui.edit, 0, sizeof(ui.edit));
			skedTimeDefault(&ui.edit.time);
			ui.edit.channel.zoneNumber = -1;
		}
		ui.field = SK_FIELD_PERIOD;
		ui.mode = SK_MODE_EDIT;
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
	{
		if (fieldSkedGet(selected(), &s))
		{
			ui.mode = SK_MODE_CONFIRM_DELETE;
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP); // already empty
		}
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuDataGlobal.currentItemIndex = ((selected() + 1) % FIELD_SKED_SLOTS);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuDataGlobal.currentItemIndex = ((selected() + FIELD_SKED_SLOTS - 1) % FIELD_SKED_SLOTS);
		updateScreen();
	}
}

menuStatus_t menuSked(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		memset(&ui, 0, sizeof(ui));
		menuDataGlobal.numItems = FIELD_SKED_SLOTS;
		if ((menuDataGlobal.currentItemIndex < 0) || (menuDataGlobal.currentItemIndex >= FIELD_SKED_SLOTS))
		{
			menuDataGlobal.currentItemIndex = 0;
		}
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
			if (ui.mode == SK_MODE_EDIT)
			{
				handleEdit(ev);
			}
			else
			{
				handleList(ev);
			}
			return MENU_STATUS_SUCCESS;
		}
	}

	// count-downs and the UTC clock
	if ((ui.mode == SK_MODE_LIST) && (ui.drawnSecs != uiDataGlobal.dateTimeSecs))
	{
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuSked(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
