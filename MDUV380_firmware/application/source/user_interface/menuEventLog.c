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
			return THEME_ITEM_FG_RSSI_BAR_S9P;
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

static void updateScreen(void)
{
	char buf[32];
	eventLogRecord_t rec;
	uint32_t count = eventLogCount();

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
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 1), rec.text, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
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
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					eventLogClear();
					selected = 0;
					soundSetMelody(MELODY_ACK_BEEP);
				}
				confirmClear = false;
				updateScreen();
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
