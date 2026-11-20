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

				if (lastIndex != -1)
				{
					menuName = (currentLanguage->LANGUAGE_NAME +
							(menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1]]->items[lastIndex].stringOffset * LANGUAGE_TEXTS_LENGTH));
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
		case MENU_EVENT_LOG:          return UI_ICON_LOG;
		case MENU_PACE:               return UI_ICON_PACE;
		case MENU_POSITION:           return UI_ICON_POSITION;
		case MENU_COVERAGE:           return UI_ICON_COVERAGE;
		case MENU_RADIO_CHECK:        return UI_ICON_RADIOCHECK;
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
	if ((sel < menuDataGlobal.numItems) && (menuDataGlobal.currentMenuList[sel].stringOffset >= 0))
	{
		const char *name = (currentLanguage->LANGUAGE_NAME + (menuDataGlobal.currentMenuList[sel].stringOffset * LANGUAGE_TEXTS_LENGTH));

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
		updateGridScreen(mName);

		if ((menuDataGlobal.currentItemIndex < menuDataGlobal.numItems) && (menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].stringOffset >= 0))
		{
			if (!isFirstRun)
			{
				voicePromptsInit();
			}
			voicePromptsAppendLanguageString(currentLanguage->LANGUAGE_NAME + (menuDataGlobal.currentMenuList[menuDataGlobal.currentItemIndex].stringOffset * LANGUAGE_TEXTS_LENGTH));
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
			if (menuDataGlobal.currentMenuList[mNum].stringOffset >= 0)
			{
				const char *menuName = (currentLanguage->LANGUAGE_NAME + (menuDataGlobal.currentMenuList[mNum].stringOffset * LANGUAGE_TEXTS_LENGTH));
				menuDisplayEntry(i, mNum, menuName, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);

				if (i == 0)
				{
					if (!isFirstRun)
					{
						voicePromptsInit();
					}

					voicePromptsAppendLanguageString(menuName);
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
