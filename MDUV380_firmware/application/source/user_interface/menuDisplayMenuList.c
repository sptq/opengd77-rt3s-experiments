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
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiEmcom.h"
#include "user_interface/uiIcons.h"

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);

static menuStatus_t menuDisplayListExitCode = MENU_STATUS_SUCCESS;

menuStatus_t menuDisplayMenuList(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		const char *menuName = NULL;
		int currentMenuNumber = menuSystemGetCurrentMenuNumber();

		menuDataGlobal.currentMenuList = (menuItemNewData_t *)menuDataGlobal.data[currentMenuNumber]->items;
		menuDataGlobal.numItems = menuDataGlobal.data[currentMenuNumber]->numItems;

		if ((currentMenuNumber != MENU_MAIN_MENU) && (menuDataGlobal.controlData.stackPosition >= 1))
		{
			if (menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1]] != NULL)
			{
				int lastIndex = menuSystemGetLastItemIndex(menuDataGlobal.controlData.stackPosition - 1);

				if ((lastIndex != -1) && (menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1]]->items[lastIndex].stringOffset >= 0))
				{
					menuName = menuItemName(&menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1]]->items[lastIndex]);
				}
			}
		}

		voicePromptsInit();
		if (menuName)
		{
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendLanguageString((const char *)menuName);
		}
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateScreen(true);

		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuDisplayListExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}
	return menuDisplayListExitCode;
}

#if defined(EMCOM_UI_MONO)
// Fork: 128 x 64 list (mockup M1): title with the position, five rows "n Name" (1..9, 0 = the
// number key that opens the entry), scroll bar on the right, key bar at the bottom
#define LIST_ROWS 5

static void updateMonoListScreen(const char *title)
{
	int sel = menuDataGlobal.currentItemIndex;
	int n = menuDataGlobal.numItems;
	int top = CLAMP((sel - (LIST_ROWS / 2)), 0, MAX(0, (n - LIST_ROWS)));
	char buf[SCREEN_LINE_BUFFER_SIZE + 4];

	displayClearBuf();
	menuDisplayTitle(title);

	for (int i = 0; i < LIST_ROWS; i++)
	{
		int k = (top + i);
		int16_t y = (int16_t)(10 + (i * 9));

		if (k >= n)
		{
			break;
		}

		const char *name = menuItemName(&menuDataGlobal.currentMenuList[k]);
		char num = ((k < 10) ? ((k == 9) ? '0' : (char)('1' + k)) : ' ');

		snprintf(buf, sizeof(buf), "%c %s", num, (name ? name : ""));
		buf[20] = 0;
		if (k == sel)
		{
			displayFillRect(0, y, (DISPLAY_SIZE_X - 4), 9, false);
		}
		displayPrintCore(2, (y + 1), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, (k == sel));
	}

	// scroll bar
	if (n > LIST_ROWS)
	{
		int16_t trackH = (LIST_ROWS * 9);
		int16_t thumbH = (int16_t)MAX(4, ((trackH * LIST_ROWS) / n));
		int16_t thumbY = (int16_t)(10 + (((trackH - thumbH) * top) / (n - LIST_ROWS)));

		displayFillRect((DISPLAY_SIZE_X - 2), 10, 1, trackH, false);
		displayFillRect((DISPLAY_SIZE_X - 3), thumbY, 3, thumbH, false);
	}

	emcomUiKeyBar("GRN open  A back");
	displayRender();
}
#endif

#if defined(HAS_EMCOM_UI)
// Fork: EMCOM style launcher: 4 x 3 tiles with an icon and the key number (1..9, 0),
// the full name of the selected entry in the bottom bar.
#define GRID_COLS      4
#define GRID_ROWS      3
#define TILE_W         38
#define TILE_H         30
#define TILE_X0        2
#define TILE_Y0        (MENU_HEADER_HEIGHT + 2)
#define NAME_BAR_Y     (TILE_Y0 + (GRID_ROWS * (TILE_H + 1)) + 1)

static uiIcon_t menuIcon(int menuNum)
{
	switch (menuNum)
	{
		case MENU_ZONE_LIST:          return UI_ICON_ZONE;
		case MENU_CONTACTS_MENU:      return UI_ICON_CONTACTS;
		case MENU_CHANNEL_DETAILS:    return UI_ICON_CHANNEL;
		case MENU_RSSI_SCREEN:        return UI_ICON_RSSI;
		case MENU_FIRMWARE_INFO:      return UI_ICON_INFO;
		case MENU_OPTIONS:            return UI_ICON_OPTIONS;
		case MENU_LAST_HEARD:         return UI_ICON_LASTHEARD;
		case MENU_RADIO_INFOS:        return UI_ICON_RADIOINFO;
		case MENU_SATELLITE:          return UI_ICON_SATELLITE;
#if defined(HAS_GPS)
		case MENU_GPS:                return UI_ICON_GPS;
#endif
#if defined(HAS_SSTV_MENU)
		case MENU_SSTV:               return UI_ICON_SSTV;
		case MENU_APRS_RX:            return UI_ICON_APRS;
		case MENU_DECODERS:           return UI_ICON_DTMF;
#endif
#if defined(HAS_DMR_SMS_MENU)
		case MENU_DMR_SMS:            return UI_ICON_MAIL;
#endif
#if defined(HAS_FIELD_TOOLS_MENU)
		case MENU_EVENT_LOG:          return UI_ICON_LOG;
		case MENU_PACE:               return UI_ICON_PACE;
		case MENU_POSITION:           return UI_ICON_POSITION;
		case MENU_COVERAGE:           return UI_ICON_COVERAGE;
		case MENU_RADIO_CHECK:        return UI_ICON_RADIOCHECK;
		case MENU_NET_CONTROL:        return UI_ICON_NET;
#endif
		case MENU_CONTACT_LIST:       return UI_ICON_CONTACTLIST;
		case MENU_DTMF_CONTACT_LIST:  return UI_ICON_DTMF;
		case MENU_CONTACT_NEW:        return UI_ICON_NEWCONTACT;
		case MENU_GENERAL:            return UI_ICON_GENERAL;
		case MENU_RADIO:              return UI_ICON_RADIO;
		case MENU_DISPLAY:            return UI_ICON_DISPLAY;
		case MENU_SOUND:              return UI_ICON_SOUND;
		case MENU_LANGUAGE:           return UI_ICON_LANGUAGE;
		case MENU_CALIBRATION:        return UI_ICON_CALIBRATION;
#if defined(HAS_COLOURS)
		case MENU_THEME:              return UI_ICON_THEME;
#endif
#if !defined(PLATFORM_GD77S)
		case MENU_APRS:               return UI_ICON_APRSCFG;
#endif
		case MENU_RESET_CUSTOM_DATA:  return UI_ICON_RESET;
		case MENU_QUICKKEYS:          return UI_ICON_QUICKKEYS;
		default:                      return UI_ICON_DEFAULT;
	}
}

static void drawIcon(int16_t x, int16_t y, uiIcon_t icon, bool inverted)
{
	for (int16_t r = 0; r < 16; r++)
	{
		uint16_t bits = uiIcons[icon][r];

		for (int16_t b = 0; b < 16; b++)
		{
			if (bits & (0x8000U >> b))
			{
				displaySetPixel((x + b), (y + r), inverted);
			}
		}
	}
}

#if defined(EMCOM_UI_MONO)
// Fork: 128 x 64 launcher (mockup M2): 4 x 2 tiles with the 16 x 16 icon and the key number,
// the selected tile dark; the name of the selected entry and the keys under them
#define MT_COLS   4
#define MT_ROWS   2
#define MT_W      31
#define MT_H      22
#define MT_Y0     10

static void updateMonoTileScreen(const char *title)
{
	int sel = menuDataGlobal.currentItemIndex;
	int n = menuDataGlobal.numItems;
	int topRow = (((sel / MT_COLS) > (MT_ROWS - 1)) ? ((sel / MT_COLS) - (MT_ROWS - 1)) : 0);

	displayClearBuf();
	menuDisplayTitle(title);

	for (int k = 0; k < n; k++)
	{
		int row = ((k / MT_COLS) - topRow);
		int col = (k % MT_COLS);

		if ((row < 0) || (row >= MT_ROWS))
		{
			continue;
		}

		int16_t x = (int16_t)(col * (MT_W + 1));
		int16_t y = (int16_t)(MT_Y0 + (row * (MT_H + 1)));
		bool selected = (k == sel);

		if (selected)
		{
			displayFillRect(x, y, MT_W, MT_H, false);
		}
		else
		{
			displayDrawRect(x, y, MT_W, MT_H, true);
		}
		drawIcon((x + 13), (y + 3), menuIcon(menuDataGlobal.currentMenuList[k].menuNum), (selected == false));

		if (k < 10)
		{
			char num[2] = { (char)((k == 9) ? '0' : ('1' + k)), 0 };

			displayPrintCore((x + 2), (y + 2), num, FONT_SIZE_1, TEXT_ALIGN_LEFT, selected);
		}
	}

	// name of the selected entry, the keys on the right
	for (int16_t dx = 0; dx < DISPLAY_SIZE_X; dx += 2)
	{
		displaySetPixel(dx, 55, true);
	}
	if (sel < n)
	{
		const char *name = menuItemName(&menuDataGlobal.currentMenuList[sel]);
		char buf[16];

		snprintf(buf, sizeof(buf), "%s", (name ? name : ""));
		buf[13] = 0;
		displayPrintCore(0, 56, buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
	}
	displayPrintCore((DISPLAY_SIZE_X - (6 * 6)), 56, "A back", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayRender();
}
#endif
#endif

#if defined(HAS_EMCOM_COLOUR_UI)
static void updateGridScreen(const char *title)
{
	int sel = menuDataGlobal.currentItemIndex;
	int topRow = ((sel / GRID_COLS) > (GRID_ROWS - 1)) ? ((sel / GRID_COLS) - (GRID_ROWS - 1)) : 0;

	displayClearBuf();
	menuDisplayTitle(title);

	for (int k = 0; k < menuDataGlobal.numItems; k++)
	{
		int row = ((k / GRID_COLS) - topRow);
		int col = (k % GRID_COLS);

		if ((row < 0) || (row >= GRID_ROWS))
		{
			continue;
		}

		int16_t x = (int16_t)(TILE_X0 + (col * (TILE_W + 1)));
		int16_t y = (int16_t)(TILE_Y0 + (row * (TILE_H + 1)));
		bool selected = (k == sel);

		displayThemeApply((selected ? THEME_ITEM_BG_MENU_ITEM_SELECTED : THEME_ITEM_BG_HEADER_TEXT), THEME_ITEM_BG);
		displayFillRect(x, y, TILE_W, TILE_H, false);

		// icon: foreground colour, or the background colour on the selected tile
		// (displaySetPixel(..., true) draws the foreground colour)
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		drawIcon((x + 11), (y + 8), menuIcon(menuDataGlobal.currentMenuList[k].menuNum), (selected == false));

		if (k < 10)
		{
			char num[2] = { (char)((k == 9) ? '0' : ('1' + k)), 0 };

			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayPrintCore((x + 2), (y + 1), num, FONT_SIZE_1, TEXT_ALIGN_LEFT, selected);
		}
	}

	// scroll marks
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	if (topRow > 0)
	{
		displayFillTriangle((DISPLAY_SIZE_X - 5), (TILE_Y0 - 1), (DISPLAY_SIZE_X - 8), (TILE_Y0 + 2), (DISPLAY_SIZE_X - 2), (TILE_Y0 + 2), true);
	}
	if ((((menuDataGlobal.numItems - 1) / GRID_COLS) - topRow) >= GRID_ROWS)
	{
		displayFillTriangle((DISPLAY_SIZE_X - 5), (NAME_BAR_Y - 1), (DISPLAY_SIZE_X - 8), (NAME_BAR_Y - 4), (DISPLAY_SIZE_X - 2), (NAME_BAR_Y - 4), true);
	}

	// name of the selected entry
	displayThemeApply(THEME_ITEM_BG_MENU_NAME, THEME_ITEM_BG);
	displayFillRect(0, NAME_BAR_Y, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - NAME_BAR_Y), false);
	if ((sel < menuDataGlobal.numItems) && (menuItemName(&menuDataGlobal.currentMenuList[sel]) != NULL))
	{
		const char *name = menuItemName(&menuDataGlobal.currentMenuList[sel]);

		displayThemeApply(THEME_ITEM_FG_MENU_NAME, THEME_ITEM_BG_MENU_NAME);
		displayPrintCore(4, (NAME_BAR_Y + ((DISPLAY_SIZE_Y - NAME_BAR_Y - FONT_SIZE_3_HEIGHT) / 2)), name, FONT_SIZE_3, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();
	displayRender();
}
#endif

static void updateScreen(bool isFirstRun)
{
	int mNum;
	const char *mName = currentLanguage->menu;

	displayClearBuf();

	// Apply some menu title override(s)
	switch (menuSystemGetCurrentMenuNumber())
	{
		case MENU_CONTACTS_MENU:
			mName = currentLanguage->contacts;
			break;
		case MENU_OPTIONS:
			mName = currentLanguage->options;
			break;
	}

#if defined(HAS_EMCOM_UI)
	if (emcomStyleEnabled())
	{
#if defined(EMCOM_UI_MONO)
		if (menuSystemGetCurrentMenuNumber() == MENU_MAIN_MENU)
		{
			updateMonoTileScreen(mName);
		}
		else
		{
			updateMonoListScreen(mName);
		}
#else
		updateGridScreen(mName);
#endif

		if ((menuDataGlobal.currentItemIndex < menuDataGlobal.numItems) && (menuItemName(&menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex]) != NULL))
		{
			if (!isFirstRun)
			{
				voicePromptsInit();
			}
			menuItemNamePrompt(&menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex]);
			promptsPlayNotAfterTx();
		}
		return;
	}
#endif

	menuDisplayTitle(mName);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(menuDataGlobal.numItems, i);
		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (mNum < menuDataGlobal.numItems)
		{
			if (menuItemName(&menuDataGlobal.currentMenuList[mNum]) != NULL)
			{
				const char *menuName = menuItemName(&menuDataGlobal.currentMenuList[mNum]);
				menuDisplayEntry(i, mNum, menuName, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);

				if (i == 0)
				{
					if (!isFirstRun)
					{
						voicePromptsInit();
					}

					menuItemNamePrompt(&menuDataGlobal.currentMenuList[mNum]);
					promptsPlayNotAfterTx();
				}
			}
		}
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
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
			updateScreen(false);
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < menuDataGlobal.numItems))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
			updateScreen(false);
		}
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
		updateScreen(false);
		menuDisplayListExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
		updateScreen(false);
		menuDisplayListExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].menuNum != -1)
		{
			menuSystemPushNewMenu(menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].menuNum);
		}
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) && (menuSystemGetCurrentMenuNumber() == MENU_MAIN_MENU))
	{
		keypadLocked = true;
		menuSystemPopAllAndDisplayRootMenu();
		menuSystemPushNewMenu(UI_LOCK_SCREEN);
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH) && (menuSystemGetCurrentMenuNumber() == MENU_MAIN_MENU))
	{
		PTTLocked = !PTTLocked;
		menuSystemPopAllAndDisplayRootMenu();
		menuSystemPushNewMenu(UI_LOCK_SCREEN);
		return;
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		if (menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].menuNum != -1)
		{
			saveQuickkeyMenuIndex(ev->keys.key, menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].menuNum, 0, 0);
		}
		return;
	}
#if defined(HAS_EMCOM_UI)
	else if (emcomStyleEnabled() && KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == false))
	{
		// Fork: key number opens the tile directly (1..9, 0 = tenth)
		int idx = ((ev->keys.key == KEY_0) ? 9 : (ev->keys.key - KEY_1));

		if ((idx >= 0) && (idx < menuDataGlobal.numItems) && (menuDataGlobal.currentMenuList[idx].menuNum != -1))
		{
			menuDataGlobal.currentItemIndex = idx;
			menuSystemPushNewMenu(menuDataGlobal.currentMenuList[idx].menuNum);
		}
		return;
	}
#endif
}
