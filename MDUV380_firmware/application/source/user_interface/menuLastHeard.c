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
#include "user_interface/uiEmcom.h"
#include "functions/netControl.h"


#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD2017)
#if defined(PLATFORM_VARIANT_DM1701)
static const int DISPLAYED_LINES_BASE = 6;
#else
static const int DISPLAYED_LINES_BASE = 7;
#endif
#else
static const int DISPLAYED_LINES_BASE = 3;
#endif

#if defined(HAS_EMCOM_COLOUR_UI)
// Fork: the EMCOM style keeps the bottom rows for the key bar
#define EMCOM_LINES_MAX ((EMCOM_UI_KEYBAR_Y - 16) / MENU_ENTRY_HEIGHT)
#define DISPLAYED_LINES_MAX ((emcomStyleEnabled() && (DISPLAYED_LINES_BASE > EMCOM_LINES_MAX)) ? EMCOM_LINES_MAX : DISPLAYED_LINES_BASE)
#else
#define DISPLAYED_LINES_MAX DISPLAYED_LINES_BASE
#endif

static bool displayLHDetails = false;
static bool confirmClear = false; // long '#' pressed: GREEN clears the list
static menuStatus_t menuLastHeardExitCode;
static LinkItem_t *selectedItem;
static int lastHeardCount;
static int firstDisplayed;

static void displayTalkerAlias(uint8_t y, char *text, uint32_t time, uint32_t now, uint32_t TGorPC, size_t maxLen, bool displayDetails, bool itemIsSelected, bool isFirstRun, LinkItem_t * item);
static void promptsInit(bool isFirstRun);

menuStatus_t menuLastHeard(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		menuLastHeardInit();
		menuLastHeardUpdateScreen(true, displayLHDetails, true);
		m = ev->time;

		return (menuLastHeardExitCode | (uiDataGlobal.lastHeardCount ? MENU_STATUS_LIST_TYPE : 0));
	}
	else
	{
		bool headHasChanged = (menuDataGlobal.startIndex != LinkHead->id);

		menuLastHeardExitCode = MENU_STATUS_SUCCESS;

		// do live update by checking if the item at the top of the list has changed
		if (headHasChanged || (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA))
		{
			bool backlightOn = false;

			// the LH list has grown
			if (lastHeardCount != uiDataGlobal.lastHeardCount)
			{
				backlightOn = true;
				lastHeardCount = uiDataGlobal.lastHeardCount;
			}
			else
			{
				// Do not turn the backlight ON on caller data updates
				if (headHasChanged)
				{
					backlightOn = true;
				}
			}

			menuDataGlobal.startIndex = LinkHead->id;

			if (backlightOn)
			{
				displayLightTrigger(false);
			}

			menuLastHeardUpdateScreen(true, displayLHDetails, false);
		}

		if (ev->hasEvent)
		{
			m = ev->time;
			menuLastHeardHandleEvent(ev);
		}
		else
		{
			// Just refresh the list while displaying details, it's all about elapsed time
			if (displayLHDetails && ((ev->time - m) > (1000U * 60U)))
			{
				m = ev->time;
				menuLastHeardUpdateScreen(true, displayLHDetails, false);
			}
		}

	}

	return menuLastHeardExitCode;
}

#if defined(HAS_EMCOM_COLOUR_UI)
// Fork: EMCOM style row: callsign (8x16) on the left, TG / PC and age on the right, two small lines;
// details: rest of the alias and the DMR ID. The classic row is drawn off-screen first for its voice prompts.
static void emcomLastHeardRow(int16_t y, const char *text, LinkItem_t *item, uint32_t now, bool displayDetails, bool selected)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char call[SCREEN_LINE_BUFFER_SIZE];
	uint32_t tg = (item->talkGroupOrPcId & 0xFFFFFF);
	bool isPC = ((item->talkGroupOrPcId >> 24) == PC_CALL_FLAG);
	uint32_t mins = (((now - item->time) / 1000U) / 60U);
	const char *ts = ((item->dmrMode == DMR_MODE_RMO) ? ((item->receivedTS == 0) ? "TS1 " : "TS2 ") : "");

	if (selected)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRect(0, y, DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT, false);
		displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG);
		displayFillRect(0, (y + 3), 2, (MENU_ENTRY_HEIGHT - 6), false);
	}
	else
	{
		displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG);
		displayFillRect(0, y, DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT, false);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine(0, (y + MENU_ENTRY_HEIGHT - 1), DISPLAY_SIZE_X, true);
	}

	getCallsignOnly(call, text);
	call[10] = 0;

	if (displayDetails)
	{
		const char *rest = text;
		while (*rest && (*rest != ' '))
		{
			rest++;
		}
		while (*rest == ' ')
		{
			rest++;
		}
		snprintf(buf, 17, "%s", (*rest ? rest : call));
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(6, y, buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, selected);
		snprintf(buf, sizeof(buf), "ID %u", (unsigned)(item->id & 0xFFFFFF));
		displayThemeApply((selected ? THEME_ITEM_FG_DEFAULT : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG);
		displayPrintCore(6, (y + 8), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, selected);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(6, y, call, FONT_SIZE_3, TEXT_ALIGN_LEFT, selected);
	}

	// right column: TG / PRIVATE, then TS and age
	if (isPC)
	{
		snprintf(buf, sizeof(buf), "PRIVATE");
	}
	else
	{
		snprintf(buf, sizeof(buf), "TG%u", (unsigned)tg);
	}
	displayThemeApply((selected ? THEME_ITEM_FG_DEFAULT : (isPC ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_RX_FREQ)), THEME_ITEM_BG);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 6)), y, buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, selected);

	if (mins < 60U)
	{
		snprintf(buf, sizeof(buf), "%s%um", ts, (unsigned)mins);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s%uh", ts, (unsigned)(mins / 60U));
	}
	displayThemeApply((selected ? THEME_ITEM_FG_DEFAULT : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 6)), (y + 8), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, selected);
	displayThemeResetToDefault();
}
#endif

void menuLastHeardUpdateScreen(bool showTitleOrHeader, bool displayDetails, bool isFirstRun)
{
	int numDisplayed = 0;
	LinkItem_t *item = LinkHead;
	uint32_t now = ticksGetMillis();
	bool invertColour;
	bool displayTA;

	// Jumping here from <SK2> + 3, with an empty heard list won't announce "Last Heard", handle this here
	if (isFirstRun && (uiDataGlobal.lastHeardCount == 0) && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		promptsInit(isFirstRun);
	}

	displayClearBuf();
	if (showTitleOrHeader)
	{
		menuDisplayTitle(currentLanguage->last_heard);
	}
	else
	{
		uiUtilityRenderHeader(false, false, false);
	}

	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);

#if defined(EMCOM_UI_MONO)
	// Fork: say why the list is empty
	if (uiDataGlobal.lastHeardCount == 0)
	{
		displayPrintCentered(22, "Nothing heard yet", FONT_SIZE_1);
		displayPrintCentered(34, "DMR stations heard", FONT_SIZE_1);
		displayPrintCentered(43, "on air appear here", FONT_SIZE_1);
	}
#endif

	// skip over the first menuDataGlobal.currentItemIndex in the listing
	for(int i = 0; i < firstDisplayed; i++)
	{
		item = item->next;
	}

	if (uiDataGlobal.lastHeardCount > 0)
	{
		while((item != NULL) && (item->id != 0) && (numDisplayed < DISPLAYED_LINES_MAX))
		{
			displayTA = false;

			if (menuDataGlobal.currentItemIndex == (firstDisplayed + numDisplayed))
			{
				invertColour = true;
				displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG_MENU_ITEM_SELECTED);
				displayFillRect(0, 16 + (numDisplayed * MENU_ENTRY_HEIGHT), DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT,
#if defined(HAS_COLOURS)
						true
#else
						false
#endif
						);
				displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
				selectedItem = item;
			}
			else
			{
				invertColour = false;
			}

			switch (nonVolatileSettings.contactDisplayPriority)
			{
				case CONTACT_DISPLAY_PRIO_CC_DB_TA:
				case CONTACT_DISPLAY_PRIO_DB_CC_TA:
					// No contact found in codeplug and DMRIDs, use TA as fallback, if any.
					if ((strncmp(item->contact, "ID:", 3) == 0) && (item->talkerAlias[0] != 0x00))
					{
						displayTA = true;
					}
					break;
				case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				case CONTACT_DISPLAY_PRIO_TA_DB_CC:
					if (item->talkerAlias[0] != 0x00)
					{
						displayTA = true;
					}
					break;
			}

#if defined(HAS_EMCOM_COLOUR_UI)
			bool emcomRow = (emcomStyleEnabled() && (uiNotificationScratchBuffer() != NULL));
			if (emcomRow)
			{
				displayOverrideScreenBuffer(uiNotificationScratchBuffer()); // classic row: voice prompts only
			}
#endif
			if (displayTA)
			{
				displayTalkerAlias(16 + (numDisplayed * MENU_ENTRY_HEIGHT) + LH_ENTRY_V_OFFSET, item->talkerAlias, item->time, now, item->talkGroupOrPcId, 32, displayDetails, invertColour, isFirstRun, item);
			}
			else
			{
				displayTalkerAlias(16 + (numDisplayed * MENU_ENTRY_HEIGHT) + LH_ENTRY_V_OFFSET, item->contact, item->time, now, item->talkGroupOrPcId, MAX_DMR_ID_CONTACT_TEXT_LENGTH, displayDetails, invertColour, isFirstRun, item);
			}
#if defined(HAS_EMCOM_COLOUR_UI)
			if (emcomRow)
			{
				displayRestorePrimaryScreenBuffer();
				emcomLastHeardRow((int16_t)(16 + (numDisplayed * MENU_ENTRY_HEIGHT)), (displayTA ? item->talkerAlias : item->contact), item, now, displayDetails, invertColour);
				displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			}
#endif

			numDisplayed++;

			item = item->next;
		}
	}
	else
	{
		promptsPlayNotAfterTx();
	}

#if defined(HAS_EMCOM_COLOUR_UI)
	if (emcomStyleEnabled() && showTitleOrHeader && (confirmClear == false))
	{
#if defined(HAS_NET_CONTROL)
		emcomUiKeyBar((uiDataGlobal.lastHeardCount > 0) ? "GRN CALL  * QSO  RED EXIT" : "RED EXIT");
#else
		emcomUiKeyBar((uiDataGlobal.lastHeardCount > 0) ? "GRN CALL  RED EXIT" : "RED EXIT");
#endif
	}
#endif

	if (confirmClear && showTitleOrHeader)
	{
		// Fork: clear confirmation over the bottom of the list
		int16_t fy = (DISPLAY_SIZE_Y - 20);

		displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
		displayFillRect(0, fy, DISPLAY_SIZE_X, 20, false);
		displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 1), "CLEAR LAST HEARD?", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 11), "GREEN yes, other key no", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();

	displayRender();
	uiDataGlobal.displayQSOState = QSO_DISPLAY_IDLE;
}

void menuLastHeardHandleEvent(uiEvent_t *ev)
{
	bool isDirty = false;
	int currentMenu = menuSystemGetCurrentMenuNumber();

	if (currentMenu == MENU_LAST_HEARD)
	{
		if (ev->events & BUTTON_EVENT)
		{
			if (repeatVoicePromptOnSK1(ev))
			{
				return;
			}
		}

		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			menuLastHeardUpdateScreen(true, displayLHDetails, false);
			return;
		}

		if (confirmClear)
		{
			// decided on a key release (the press of GREEN would cancel), but not the release of the long '#'
			if ((ev->keys.event & KEY_MOD_UP) && (((ev->keys.key == KEY_HASH) && (ev->keys.event & KEY_MOD_LONG)) == false))
			{
				confirmClear = false;

				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					lastHeardInitList();
					lastHeardClearLastID();
					menuLastHeardInit();
					promptsInit(true); // Stack "Empty List" VP.
				}

				menuLastHeardUpdateScreen(true, displayLHDetails, false);

				if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					voicePromptsPlay();
				}
			}
			return;
		}
	}

	if (uiDataGlobal.lastHeardCount > 0)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			if (menuDataGlobal.currentItemIndex < (uiDataGlobal.lastHeardCount - 1))
			{
				isDirty = true;
				menuDataGlobal.currentItemIndex++;

				if (menuDataGlobal.currentItemIndex >= DISPLAYED_LINES_MAX)
				{
					if (((menuDataGlobal.currentItemIndex - DISPLAYED_LINES_MAX) == firstDisplayed) &&
							(menuDataGlobal.currentItemIndex <= (uiDataGlobal.lastHeardCount - 1)))
					{
						firstDisplayed++;
					}
				}
			}

			if (menuDataGlobal.currentItemIndex == 0)
			{
				menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			isDirty = true;
			menuDataGlobal.currentItemIndex = (uiDataGlobal.lastHeardCount - 1);
			firstDisplayed = SAFE_MAX((menuDataGlobal.currentItemIndex - (DISPLAYED_LINES_MAX - 1)), 0);

			if (menuDataGlobal.currentItemIndex == 0)
			{
				menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			if (menuDataGlobal.currentItemIndex > 0)
			{
				isDirty = true;
				menuDataGlobal.currentItemIndex--;

				if (firstDisplayed && (menuDataGlobal.currentItemIndex < firstDisplayed))
				{
					firstDisplayed--;
				}
			}

			if (menuDataGlobal.currentItemIndex == 0)
			{
				menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			menuDataGlobal.currentItemIndex = 0;
			isDirty = true;
			firstDisplayed = 0;
			menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if ((currentMenu == MENU_LAST_HEARD) && KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			int timeslot = selectedItem->receivedTS;
			uint32_t tgOrPC = ((BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) ? selectedItem->talkGroupOrPcId : ((PC_CALL_FLAG << 24) | selectedItem->id));

			setOverrideTGorPC(tgOrPC, ((tgOrPC >> 24) == PC_CALL_FLAG));

			if ((timeslot != -1) && (timeslot != trxGetDMRTimeSlot()))
			{
				trxSetDMRTimeSlot(timeslot, true);
				tsSetManualOverride(((menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE) ? CHANNEL_CHANNEL : (CHANNEL_VFO_A + nonVolatileSettings.currentVFONumber)), (timeslot + 1));
			}

			announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
			menuSystemPopAllAndDisplayRootMenu();
			return;
		}
#if defined(HAS_NET_CONTROL)
		else if ((currentMenu == MENU_LAST_HEARD) && KEYCHECK_SHORTUP(ev->keys, KEY_STAR) && (BUTTONCHECK_DOWN(ev, (BUTTON_SK1 | BUTTON_SK2)) == 0) &&
				(selectedItem != NULL)) // Fork: log a QSO with the selected station (report 59)
		{
			char call[SCREEN_LINE_BUFFER_SIZE];

			netStationName(selectedItem, call, sizeof(call));
			menuLastHeardUpdateScreen(true, displayLHDetails, false);
			netLogQso(call);
			return;
		}
#endif
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH) && (BUTTONCHECK_DOWN(ev, (BUTTON_SK1 | BUTTON_SK2)) == 0)) // Clear LH list
		{
			if (currentMenu == MENU_LAST_HEARD) // Only allowed within LH menu
			{
				confirmClear = true; // Fork: cleared after GREEN (see above)
				isDirty = true;
			}
		}
	}

	if (currentMenu == MENU_LAST_HEARD)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
		{
			saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			return;
		}

		// Toggles LH simple/detailed views on SK2 press
		if (!displayLHDetails && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			isDirty = true;
			displayLHDetails = true;
		}
		else if (displayLHDetails && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			isDirty = true;
			displayLHDetails = false;
		}

		if (isDirty)
		{
			menuLastHeardUpdateScreen(true, displayLHDetails, false);// This will also setup the voice prompt

			if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
			{
				voicePromptsPlay();
			}
		}
	}
	else
	{
		if (isDirty)
		{
			menuLastHeardUpdateScreen(false, false, false);
		}
	}
}

void menuLastHeardInit(void)
{
	menuDataGlobal.startIndex = LinkHead->id;// reuse this global to store the ID of the first item in the list
	menuDataGlobal.currentItemIndex = 0;
	menuDataGlobal.numItems = uiDataGlobal.lastHeardCount;
	selectedItem = NULL;
	firstDisplayed = 0;
	displayLHDetails = false;
	confirmClear = false;
	lastHeardCount = uiDataGlobal.lastHeardCount;
	menuLastHeardExitCode = MENU_STATUS_SUCCESS;
}

static void promptsInit(bool isFirstRun)
{
	voicePromptsInit();
	if (isFirstRun)
	{
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->last_heard);
		voicePromptsAppendLanguageString(currentLanguage->menu);

		if (uiDataGlobal.lastHeardCount == 0)
		{
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendLanguageString(currentLanguage->list_empty);
		}
	}
}

static void displayTalkerAlias(uint8_t y, char *text, uint32_t time, uint32_t now, uint32_t TGorPC, size_t maxLen, bool displayDetails, bool itemIsSelected, bool isFirstRun, LinkItem_t * item)
{
	char timeBuffer[SCREEN_LINE_BUFFER_SIZE];
	uint32_t tg = (TGorPC & 0xFFFFFF);
	bool isPC = ((TGorPC >> 24) == PC_CALL_FLAG);
	bool inHours = false;
	uint32_t heardSince = (((now - time) / 1000U) / 60U);

	// If last heard time is greater than 720 minutes (12h), switch to hours view to avoid mixup between TG and time.
	if (heardSince > 720U)
	{
		heardSince /= 60U;
		inHours = true;

		if (heardSince > 999)
		{
			heardSince = 999;
		}
	}

	snprintf(timeBuffer, 6, "%u", heardSince); // Time, without unit

	if (itemIsSelected && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		promptsInit(isFirstRun);
	}

	if (!displayDetails) // search for callsign + first name
	{
		char buffer[37]; // Max: TA 27 (in 7bit format) + ' [' + 6 (Maidenhead)  + ']' + NULL

		if (strlen(text) >= 5)
		{
			int32_t  cpos;

			if ((cpos = getFirstSpacePos(text)) != -1) // Callsign found
			{
				// Check if second part is 'DMR ID:'
				// In this case, don't go further
				if (strncmp((text + cpos + 1), "DMR ID:", 7) == 0)
				{
					if (cpos > 15)
					{
						cpos = 16;
					}

					memcpy(buffer, text, cpos);
					buffer[cpos] = 0;

					displayPrintCore(0, y, chomp(buffer), FONT_SIZE_3, TEXT_ALIGN_CENTER, itemIsSelected);
				}
				else // Nope, look for first name
				{
					uint32_t npos;
					char nameBuf[SCREEN_LINE_BUFFER_SIZE];
					char outputBuf[SCREEN_LINE_BUFFER_SIZE];

					memset(nameBuf, 0, sizeof(nameBuf));

					// Look for the second part, aka first name
					if ((npos = getFirstSpacePos(text + cpos + 1)) != -1)
					{
						// Store callsign
						memcpy(buffer, text, cpos);
						buffer[cpos] = 0;

						// Store 'first name'
						memcpy(nameBuf, (text + cpos + 1), npos);
						nameBuf[npos] = 0;

						snprintf(outputBuf, SCREEN_LINE_BUFFER_SIZE, "%s %s", chomp(buffer), chomp(nameBuf));

						displayPrintCore(0, y, chomp(outputBuf), FONT_SIZE_3, TEXT_ALIGN_CENTER, itemIsSelected);
					}
					else
					{
						// Store callsign
						memcpy(buffer, text, cpos);
						buffer[cpos] = 0;

						memcpy(nameBuf, (text + cpos + 1), strlen(text) - cpos - 1);
						nameBuf[(SCREEN_LINE_BUFFER_SIZE - 1)] = 0;

						snprintf(outputBuf, SCREEN_LINE_BUFFER_SIZE, "%s %s", chomp(buffer), chomp(nameBuf));

						displayPrintCore(0, y, chomp(outputBuf), FONT_SIZE_3, TEXT_ALIGN_CENTER, itemIsSelected);
					}
				}
			}
			else
			{
				// No space found, use a chainsaw
				memcpy(buffer, text, (SCREEN_LINE_BUFFER_SIZE - 1));
				buffer[(SCREEN_LINE_BUFFER_SIZE - 1)] = 0;

				displayPrintCore(0, y, chomp(buffer), FONT_SIZE_3, TEXT_ALIGN_CENTER, itemIsSelected);
			}
		}
		else // short callsign
		{
			memcpy(buffer, text, strlen(text));
			buffer[strlen(text)] = 0;

			displayPrintCore(0, y, chomp(buffer), FONT_SIZE_3, TEXT_ALIGN_CENTER, itemIsSelected);
		}

		if (itemIsSelected && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
		{
			voicePromptsAppendString(chomp(buffer));
			voicePromptsAppendString("  ");

			snprintf(buffer, 37, "%u ", tg);
			if (isPC)
			{
				voicePromptsAppendLanguageString(currentLanguage->private_call);
				if (tg != trxDMRID)
				{
					voicePromptsAppendString(buffer);
				}
			}
			else
			{
				voicePromptsAppendPrompt(PROMPT_TALKGROUP);
				voicePromptsAppendString(buffer);
			}

			voicePromptsAppendString(timeBuffer);
			if (inHours)
			{
				voicePromptsAppendLanguageString(currentLanguage->hours);
			}
			else
			{
				voicePromptsAppendPrompt(PROMPT_MINUTES);
			}
			voicePromptsAppendString("   ");// Add some blank sound at the end of the callsign, to allow time for follow-on scrolling
		}
	}
	else
	{
		char buffer[SCREEN_LINE_BUFFER_SIZE];
		bool displayTS = (item->dmrMode == DMR_MODE_RMO);
		int16_t yTimeUnit =
#if defined(PLATFORM_RD5R)
				y;
#else
				(y + 6);
#endif

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u", tg);

		displayPrintCore(0, y, (isPC ? currentLanguage->pc : currentLanguage->tg), FONT_SIZE_3, TEXT_ALIGN_LEFT, itemIsSelected);
		displayPrintCore((2 * 8) + 4, y, buffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, itemIsSelected);

		// Display TS (if in RMO), in a reverse video box.
		if (displayTS)
		{
			int16_t yTS =
#if defined(PLATFORM_RD5R)
					y;
#else
					(y + 5);
#endif

			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d", (item->receivedTS + 1));

			displayFillRect((DISPLAY_SIZE_X - (1 * 6) - (4 * 8) - 3), yTS - 1, (6 + 1), 9, itemIsSelected);
			displayPrintCore((DISPLAY_SIZE_X - (1 * 6) - (4 * 8) - 2), yTS, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, !itemIsSelected);

		}

		displayPrintCore((DISPLAY_SIZE_X - (strlen(timeBuffer) * 8) - (1 * 6) - 1), y, timeBuffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, itemIsSelected);

		// Time unit is displayed using the smallest font
		// 'm' for minutes is not SI, but using 3 characters "min" won't fit the screen
		displayPrintCore((DISPLAY_SIZE_X - (1 * 6)), yTimeUnit, (inHours ? "h" : "m"), FONT_SIZE_1, TEXT_ALIGN_LEFT, itemIsSelected);
	}

	if (isFirstRun)
	{
		promptsPlayNotAfterTx();
	}
}
