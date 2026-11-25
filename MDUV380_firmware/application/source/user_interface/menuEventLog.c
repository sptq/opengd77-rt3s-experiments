/*
 * Fork: EMCOM event log screen (main menu > Event log), by Marcin SP7TQ.
 *
 * Newest first: UTC time, type chip, text; the selected entry in full with
 * its date in the footer. UP / DOWN scroll, '0' then GREEN clears the log,
 * RED / GREEN leave. The log itself is functions/eventLog.c.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/sound.h"
#include "functions/eventLog.h"
#include "user_interface/uiEmcom.h"

#if defined(HAS_EVENT_LOG)

#define ROW_H        (FONT_SIZE_1_HEIGHT + 2)
#define LIST_Y       (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H     20
#define ROWS         ((DISPLAY_SIZE_Y - LIST_Y - FOOTER_H) / ROW_H)

static uint32_t selected = 0;
static bool confirmClear = false;

static themeItem_t typeColour(uint8_t type)
{
	switch (type)
	{
		case EVENT_LOG_TX:
		case EVENT_LOG_SOS:
			return THEME_ITEM_FG_TX_COUNTER;
		case EVENT_LOG_RX:
		case EVENT_LOG_DMR:
		case EVENT_LOG_MSG_IN:
			return THEME_ITEM_FG_RSSI_BAR;
		case EVENT_LOG_APRS:
		case EVENT_LOG_MSG_OUT:
		case EVENT_LOG_SSTV:
			return THEME_ITEM_FG_RX_FREQ;
		case EVENT_LOG_DTMF:
		case EVENT_LOG_NET:
			return THEME_ITEM_FG_RSSI_BAR_S9P;
		case EVENT_LOG_QSO:
			return THEME_ITEM_FG_RSSI_BAR;
		default:
			return THEME_ITEM_FG_DECORATION;
	}
}

static void timeString(uint32_t t, char *buf, size_t size, bool withDate)
{
	if (t == 0U)
	{
		snprintf(buf, size, (withDate ? "no clock" : "--:--"));
		return;
	}

	struct tm tmv;
	time_t_custom secs = t;

	gmtime_r_Custom(&secs, &tmv);
	if (withDate)
	{
		snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02dZ", (tmv.tm_year + 1900), (tmv.tm_mon + 1), tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
	}
	else
	{
		snprintf(buf, size, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
	}
}

#if defined(HAS_EMCOM_UI)
#if defined(EMCOM_UI_MONO)
// 128 x 64 mono (MD-9600): title with n/count, rows of 9 px (UTC time, type tag, text), the
// selected one as a dark bar with light text; under a divider the selected entry's date and
// its full text (20 characters fit)
#define MONO_LIST_Y   10
#define MONO_ROW_H    9
#define MONO_ROWS     3U
#define MONO_TAG_X    33
#define MONO_TAG_W    21
#define MONO_TEXT_X   57
#define MONO_DATE_Y   38
#define MONO_FULL_Y   47

// Type tag: a solid block, or a light outline on the selected bar
static void typeTagMono(int16_t y, uint8_t type, bool sel)
{
	if (sel)
	{
		displayDrawRect(MONO_TAG_X, y, MONO_TAG_W, MONO_ROW_H, false);
	}
	else
	{
		displayFillRect(MONO_TAG_X, y, MONO_TAG_W, MONO_ROW_H, false);
	}
	displayPrintCore((MONO_TAG_X + 2), (y + 1), eventLogTypeName(type), FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
}

static void updateScreenMono(void)
{
	char buf[32];
	eventLogRecord_t rec;
	uint32_t count = eventLogCount();

	displayClearBuf();
	if (count == 0U)
	{
		emcomUiTitle(currentLanguage->event_log, "0");
		emcomUiChip(0, 11, "EMPTY", THEME_ITEM_FG_DECORATION, EMCOM_CHIP_OUTLINE, false);
		displayPrintCentered(21, "Log empty", FONT_SIZE_3);
		displayPrintCentered(38, "TX RX DMR APRS MSG", FONT_SIZE_1);
		displayPrintCentered(47, "DTMF SOS SSTV events", FONT_SIZE_1);
		emcomUiKeyBar("RED EXIT");
		displayRender();
		return;
	}

	if (selected >= count)
	{
		selected = (count - 1U);
	}

	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(selected + 1U), (unsigned)count);
	emcomUiTitle(currentLanguage->event_log, buf);

	uint32_t first = ((selected >= MONO_ROWS) ? (selected - MONO_ROWS + 1U) : 0U);

	for (uint32_t r = 0; r < MONO_ROWS; r++)
	{
		uint32_t idx = (first + r);
		int16_t y = (int16_t)(MONO_LIST_Y + (r * MONO_ROW_H));
		bool sel = (idx == selected);

		if ((idx >= count) || (eventLogGet(idx, &rec) == false))
		{
			break;
		}

		if (sel)
		{
			displayFillRect(0, y, DISPLAY_SIZE_X, MONO_ROW_H, false);
		}
		timeString(rec.time, buf, sizeof(buf), false);
		displayPrintCore(1, (y + 1), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
		typeTagMono(y, rec.type, sel);
		snprintf(buf, sizeof(buf), "%.11s", rec.text); // 11 characters fit
		displayPrintCore(MONO_TEXT_X, (y + 1), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
	}

	// the selected entry: date and time, the full text
	if (eventLogGet(selected, &rec))
	{
		char full[EVENT_LOG_TEXT_LEN + 1];

		emcomUiDivider(MONO_DATE_Y - 1);
		timeString(rec.time, buf, sizeof(buf), true);
		emcomUiText(0, MONO_DATE_Y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		snprintf(full, sizeof(full), "%.*s", (int)EVENT_LOG_TEXT_LEN, rec.text); // not terminated when full
		emcomUiText(0, MONO_FULL_Y, full, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
	}

	if (confirmClear)
	{
		emcomUiConfirmBar("CLEAR?");
	}
	else
	{
		emcomUiKeyBar("UP/DN 0 CLR RED EXIT");
	}
	displayRender();
}
#else
// EMCOM style: the date of the selected entry on the status strip, rows of time / type chip /
// text, the selected entry in full above the key bar (8x16 in the contrast style when it fits)
#define EM_ROW_H     11
#define EM_CHIP_X    34
#define EM_TEXT_X    60

// Type chip, 22 px wide: filled, or outlined on a selected row
static void typeChip(int16_t x, int16_t y, uint8_t type, bool sel)
{
	const char *name = eventLogTypeName(type);
	themeItem_t c = (sel ? THEME_ITEM_BG : typeColour(type));

	displayThemeApply(c, THEME_ITEM_BG);
	if (sel)
	{
		displayDrawRect(x, (y - 1), 22, (FONT_SIZE_1_HEIGHT + 1), false);
	}
	else
	{
		displayFillRect(x, (y - 1), 22, (FONT_SIZE_1_HEIGHT + 1), false);
	}
	displayPrintCore((x + 2), y, name, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, (sel == false));
	displayThemeResetToDefault();
}

static void updateScreenEmcom(void)
{
	char buf[32];
	eventLogRecord_t rec;
	uint32_t count = eventLogCount();

	displayClearBuf();
	if (count == 0U)
	{
		emcomUiTitle(currentLanguage->event_log, "0");
		emcomUiStrip();
		emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), "EMPTY", THEME_ITEM_FG_DECORATION, EMCOM_CHIP_OUTLINE, false);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((EMCOM_UI_BODY_Y + 16), "Log empty", FONT_SIZE_3);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCentered((EMCOM_UI_BODY_Y + 40), "TX, RX, DMR, APRS, MSG,", FONT_SIZE_1);
		displayPrintCentered((EMCOM_UI_BODY_Y + 50), "DTMF, SOS, SSTV events", FONT_SIZE_1);
		displayThemeResetToDefault();
		emcomUiKeyBar("RED EXIT");
		displayRender();
		return;
	}

	if (selected >= count)
	{
		selected = (count - 1U);
	}

	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(selected + 1U), (unsigned)count);
	emcomUiTitle(currentLanguage->event_log, buf);

	// the selected entry: full text big (contrast) or small, above the key bar
	bool haveSel = eventLogGet(selected, &rec);
	bool large = emcomUiLarge(); // room for an 8x16 line (the list keeps its length)
	char full[EVENT_LOG_TEXT_LEN + 1];

	snprintf(full, sizeof(full), "%.*s", (int)EVENT_LOG_TEXT_LEN, (haveSel ? rec.text : "")); // not terminated when full
	bool big = (haveSel && large && ((strlen(full) * 8) <= (DISPLAY_SIZE_X - 4)));
	int16_t detailY = (int16_t)(EMCOM_UI_KEYBAR_Y - (large ? FONT_SIZE_3_HEIGHT : FONT_SIZE_1_HEIGHT) - 2);
	uint32_t rows = (uint32_t)((detailY - 1 - EMCOM_UI_BODY_Y) / EM_ROW_H);

	if (haveSel)
	{
		emcomUiStrip();
		timeString(rec.time, buf, sizeof(buf), true);
		emcomUiText(3, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
		typeChip((DISPLAY_SIZE_X - 24), (EMCOM_UI_STRIP_Y + 2), rec.type, false);

		emcomUiDivider(detailY - 2);
		emcomUiText(2, (detailY + ((large && (big == false)) ? 4 : 0)), full, (big ? FONT_SIZE_3 : FONT_SIZE_1), THEME_ITEM_FG_DEFAULT, false);
	}

	uint32_t first = ((selected >= rows) ? (selected - rows + 1U) : 0U);

	for (uint32_t r = 0; r < rows; r++)
	{
		uint32_t idx = (first + r);
		int16_t y = (int16_t)(EMCOM_UI_BODY_Y + (r * EM_ROW_H));
		bool sel = (idx == selected);
		themeItem_t text = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT);

		if ((idx >= count) || (eventLogGet(idx, &rec) == false))
		{
			break;
		}

		if (sel)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect(0, (y - 2), DISPLAY_SIZE_X, EM_ROW_H, false);
			displayThemeResetToDefault();
		}

		timeString(rec.time, buf, sizeof(buf), false);
		emcomUiText(2, y, buf, FONT_SIZE_1, (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION), false);
		typeChip(EM_CHIP_X, y, rec.type, sel);
		snprintf(buf, sizeof(buf), "%.16s", rec.text); // 16 characters fit
		emcomUiText(EM_TEXT_X, y, buf, FONT_SIZE_1, text, false);
	}

	if (confirmClear)
	{
		emcomUiConfirmBar("CLEAR LOG?");
	}
	else
	{
		emcomUiKeyBar("UP/DN  0 CLEAR  RED EXIT");
	}
	displayRender();
}
#endif // EMCOM_UI_MONO
#endif

static void updateScreen(void)
{
	char buf[32];
	eventLogRecord_t rec;
	uint32_t count = eventLogCount();

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
	menuDisplayTitle(currentLanguage->event_log);

	snprintf(buf, sizeof(buf), "%u", (unsigned)count);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_MENU_NAME);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 6)), 4, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	if (count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((LIST_Y + 24), "Log empty", FONT_SIZE_3);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCentered((LIST_Y + 48), "TX, RX, DMR, APRS, MSG,", FONT_SIZE_1);
		displayPrintCentered((LIST_Y + 58), "DTMF, SOS, SSTV events", FONT_SIZE_1);
		displayThemeResetToDefault();
		displayRender();
		return;
	}

	if (selected >= count)
	{
		selected = (count - 1U);
	}

	uint32_t first = ((selected >= ROWS) ? (selected - ROWS + 1U) : 0U);

	for (uint32_t r = 0; r < ROWS; r++)
	{
		uint32_t idx = (first + r);
		int16_t y = (int16_t)(LIST_Y + (r * ROW_H));
		bool sel = (idx == selected);

		if ((idx >= count) || (eventLogGet(idx, &rec) == false))
		{
			break;
		}

		if (sel)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect(0, (y - 1), DISPLAY_SIZE_X, ROW_H, false);
		}

		timeString(rec.time, buf, sizeof(buf), false);
		displayThemeApply((sel ? THEME_ITEM_FG_DEFAULT : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG);
		displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);

		// type chip
		const char *name = eventLogTypeName(rec.type);
		displayThemeApply(typeColour(rec.type), THEME_ITEM_BG);
		displayFillRect(34, (y - 1), 22, (FONT_SIZE_1_HEIGHT + 1), false);
		displayPrintCore(36, y, name, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);

		snprintf(buf, 17, "%s", rec.text); // 16 characters fit
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(60, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, sel);
	}

	// Footer: the selected entry in full + its date, or the clear confirmation
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);

	if (confirmClear)
	{
		displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 1), "CLEAR THE WHOLE LOG?", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 11), "GREEN yes, other key no", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else if (eventLogGet(selected, &rec))
	{
		char full[EVENT_LOG_TEXT_LEN + 1];

		memcpy(full, rec.text, EVENT_LOG_TEXT_LEN); // not terminated when the text fills the record
		full[EVENT_LOG_TEXT_LEN] = 0;
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 1), full, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		timeString(rec.time, buf, sizeof(buf), true);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 11), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();
	displayRender();
}

menuStatus_t menuEventLog(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t lastSerial = 0;

	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		selected = 0;
		confirmClear = false;
		lastSerial = eventLogSerial();
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
			if (confirmClear)
			{
				// decided on the key release: the press of GREEN cancelled, then its release left the screen
				if (ev->keys.event & KEY_MOD_UP)
				{
					if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
					{
						eventLogClear();
						selected = 0;
						soundSetMelody(MELODY_ACK_BEEP);
					}
					confirmClear = false;
					updateScreen();
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				confirmClear = true;
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
			{
				if ((selected + 1U) < eventLogCount())
				{
					selected++;
				}
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
			{
				if (selected > 0U)
				{
					selected--;
				}
				updateScreen();
			}
		}
	}

	// New entries while the screen is shown
	uint32_t serial = eventLogSerial();
	if (serial != lastSerial)
	{
		if ((selected != 0U) && (serial > lastSerial))
		{
			selected += (serial - lastSerial); // keep the same entry selected
		}
		lastSerial = serial;
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuEventLog(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
