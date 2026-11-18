/*
 * Copyright (C) 2026 OpenGD77 RT3S experiments contributors
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the terms of the OpenGD77 modified BSD-3-Clause license,
 * non-commercial only — see the project licence for full text.
 */
#include "functions/codeplug.h"
#include "functions/settings.h"
#if !defined(PLATFORM_GD77S)
#include "functions/aprs.h"
#endif
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"

//
// Options -> Quickkeys
//
// Lists the ten SK2 + digit quickkey slots and lets the user (re)assign them
// without having to remember the per-menu "SK2 + digit" recording gesture.
//
//   UP / DOWN       select a slot
//   knob / LEFT/RIGHT  cycle the assignment (None, actions, common menus,
//                      and the slot's original assignment if it was custom)
//   GREEN           store the changed slots in the codeplug, then exit
//   RED             discard the changes and exit
//
// Quickkey *actions* are a fork extension: see QUICKKEY_ACTION_* in menuSystem.h.
//

_Static_assert(MENU_QUICKKEYS < 32, "Quickkey-addressable menus must have an ID below 32");

#define QUICKKEYS_NUM_SLOTS  10
#define QUICKKEY_EMPTY       0x8000 // Same value as saveQuickkeyMenuLongValue(key, 0, 0) writes

static uint16_t slotValues[QUICKKEYS_NUM_SLOTS];
static uint16_t slotOriginalValues[QUICKKEYS_NUM_SLOTS];
static menuStatus_t menuQuickkeysExitCode = MENU_STATUS_SUCCESS;

// Assignments offered when cycling a slot.
static const uint16_t quickkeyChoices[] =
{
	QUICKKEY_EMPTY,
#if !defined(PLATFORM_GD77S)
	QUICKKEY_ACTIONVALUE(QUICKKEY_ACTION_APRS_BEACON),
	QUICKKEY_ACTIONVALUE(QUICKKEY_ACTION_APRS_TOGGLE),
#endif
#if defined(HAS_COLOURS)
	QUICKKEY_ACTIONVALUE(QUICKKEY_ACTION_DAY_NIGHT),
#endif
	QUICKKEY_ACTIONVALUE(QUICKKEY_ACTION_BAND_SCOPE),
	QUICKKEY_ACTIONVALUE(QUICKKEY_ACTION_SWEEP_TUNE_PEAK),
	QUICKKEY_MENUVALUE(MENU_RSSI_SCREEN, 0, 0),
	QUICKKEY_MENUVALUE(MENU_LAST_HEARD, 0, 0),
	QUICKKEY_MENULONGVALUE(MENU_ZONE_LIST, 0),
	QUICKKEY_MENUVALUE(MENU_CONTACTS_MENU, 0, 0),
	QUICKKEY_MENUVALUE(MENU_RADIO_INFOS, 0, 0),
	QUICKKEY_MENUVALUE(MENU_SATELLITE, 0, 0),
#if defined(HAS_GPS)
	QUICKKEY_MENUVALUE(MENU_GPS, 0, 0),
#endif
#if !defined(PLATFORM_GD77S)
	QUICKKEY_MENUVALUE(MENU_APRS, 0, 0),
#endif
};
#define QUICKKEYS_NUM_CHOICES ((int)(sizeof(quickkeyChoices) / sizeof(quickkeyChoices[0])))

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

const char *quickkeyActionGetName(quickkeyAction_t action)
{
	switch (action)
	{
		case QUICKKEY_ACTION_APRS_BEACON:
			return "APRS beacon";
		case QUICKKEY_ACTION_APRS_TOGGLE:
			return "APRS on/off";
		case QUICKKEY_ACTION_DAY_NIGHT:
			return "Day/Night";
		case QUICKKEY_ACTION_BAND_SCOPE:
			return "Band scope";
		case QUICKKEY_ACTION_SWEEP_TUNE_PEAK:
			return "Scope: to peak";
		default:
			break;
	}

	return currentLanguage->none;
}

// Runs a quickkey action from the Channel or VFO screen.
// Returns false when the action is not applicable right now (caller beeps NACK).
bool quickkeyActionExecute(quickkeyAction_t action, bool isChannelMode)
{
	switch (action)
	{
#if !defined(PLATFORM_GD77S)
		case QUICKKEY_ACTION_APRS_BEACON:
			// Same as SK1 + 2
			switch (aprsBeaconingGetMode())
			{
				case APRS_BEACONING_MODE_MANUAL:
				case APRS_BEACONING_MODE_AUTO:
				case APRS_BEACONING_MODE_SMART_BEACONING:
					if (txInhibitCheckAndWarn() == false)
					{
						return aprsBeaconingSendBeacon(false, true);
					}
					return true; // txInhibitCheckAndWarn() already told the user

				default:
					return false;
			}
			break;

		case QUICKKEY_ACTION_APRS_TOGGLE:
			// Same as SK1 + 1
			aprsBeaconingToggles();
			return true;
#endif

#if defined(HAS_COLOURS)
		case QUICKKEY_ACTION_DAY_NIGHT:
			// Same as SK1 + RED
			uiChannelModeOrVFOModeThemeDaytimeChange(true, isChannelMode);
			return true;
#endif

		case QUICKKEY_ACTION_BAND_SCOPE:
			if (trxTransmissionEnabled)
			{
				return false;
			}

			if (uiVFOModeSweepScanning(true))
			{
				return true; // Already there
			}

			// The VFO screen starts the sweep on its next tick (same as long #).
			uiVFOModeRequestSweep();
			if (isChannelMode)
			{
				if (uiChannelModeIsScanning())
				{
					uiChannelModeStopScanning();
				}
				menuSystemPopAllAndDisplaySpecificRootMenu(UI_VFO_MODE, true);
			}
			return true;

		case QUICKKEY_ACTION_SWEEP_TUNE_PEAK:
			// Same as SK1 + 0 while sweeping
			return ((isChannelMode == false) && uiVFOModeSweepTuneToPeak());

		default:
			break;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Slot description
// ---------------------------------------------------------------------------

static bool quickkeyValueIsEmpty(uint16_t value)
{
	if (value == QUICKKEY_EMPTY)
	{
		return true;
	}

	if (QUICKKEY_TYPE(value) == QUICKKEY_CONTACT)
	{
		if (QUICKKEY_IS_ACTION(value))
		{
			return ((QUICKKEY_ACTIONID(value) == QUICKKEY_ACTION_NONE) || (QUICKKEY_ACTIONID(value) >= QUICKKEY_ACTION_MAX));
		}

		return ((value < CODEPLUG_CONTACTS_MIN) || (value > CODEPLUG_CONTACTS_MAX));
	}

	return false;
}

static const char *quickkeyMenuTitle(int menuId)
{
	const menuItemsList_t *lists[] = { &menuDataMainMenu, &menuDataOptions, &menuDataContact };

	for (int l = 0; l < (int)(sizeof(lists) / sizeof(lists[0])); l++)
	{
		for (int i = 0; i < lists[l]->numItems; i++)
		{
			if ((lists[l]->items[i].menuNum == menuId) && (lists[l]->items[i].stringOffset >= 0))
			{
				return (currentLanguage->LANGUAGE_NAME + (lists[l]->items[i].stringOffset * LANGUAGE_TEXTS_LENGTH));
			}
		}
	}

	switch (menuId)
	{
		case MENU_CONTACTS_MENU:
			return currentLanguage->contacts;
		case UI_CHANNEL_QUICK_MENU:
		case UI_VFO_QUICK_MENU:
			return currentLanguage->quick_menu;
		default:
			break;
	}

	return NULL;
}

static void quickkeyDescribe(uint16_t value, char *buf, size_t bufLen)
{
	if (quickkeyValueIsEmpty(value))
	{
		snprintf(buf, bufLen, "%s", currentLanguage->none);
		return;
	}

	if (QUICKKEY_TYPE(value) == QUICKKEY_CONTACT)
	{
		if (QUICKKEY_IS_ACTION(value))
		{
			snprintf(buf, bufLen, "%s", quickkeyActionGetName((quickkeyAction_t)QUICKKEY_ACTIONID(value)));
		}
		else
		{
			CodeplugContact_t contact;
			char nameBuf[17];

			if (codeplugContactGetDataForIndex(value, &contact))
			{
				codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
				snprintf(buf, bufLen, "%s", nameBuf);
			}
			else
			{
				snprintf(buf, bufLen, "%s %d", currentLanguage->contact, value);
			}
		}
		return;
	}

	int menuId = QUICKKEY_MENUID(value);

	if (menuId == MENU_ZONE_LIST)
	{
		int zoneEntry = QUICKKEY_LONGENTRYID(value);
		CodeplugZone_t zone;
		char nameBuf[17];

		if ((zoneEntry > 0) && (zoneEntry <= codeplugZonesGetCount()) && codeplugZoneGetDataForNumber((zoneEntry - 1), &zone))
		{
			codeplugUtilConvertBufToString(zone.name, nameBuf, 16);
			snprintf(buf, bufLen, "%s", nameBuf);
		}
		else
		{
			snprintf(buf, bufLen, "%s", currentLanguage->zones);
		}
		return;
	}

	const char *title = quickkeyMenuTitle(menuId);
	int entry = QUICKKEY_ENTRYID(value);

	if (title == NULL)
	{
		snprintf(buf, bufLen, "%s %d", currentLanguage->menu, menuId);
	}
	else if ((entry != 0) || (QUICKKEY_FUNCTIONID(value) != 0))
	{
		snprintf(buf, bufLen, "%s %d", title, (entry + 1));
	}
	else
	{
		snprintf(buf, bufLen, "%s", title);
	}
}

// ---------------------------------------------------------------------------
// Choice cycling
// ---------------------------------------------------------------------------

static int quickkeyChoiceIndex(uint16_t value)
{
	if (quickkeyValueIsEmpty(value))
	{
		return 0;
	}

	for (int i = 0; i < QUICKKEYS_NUM_CHOICES; i++)
	{
		if (quickkeyChoices[i] == value)
		{
			return i;
		}
	}

	return -1;
}

static void quickkeyCycle(int slot, bool forward)
{
	// A custom original assignment (contact, a specific zone, a menu entry...)
	// is kept reachable as an extra virtual choice at position 0.
	bool hasCustomOriginal = (quickkeyChoiceIndex(slotOriginalValues[slot]) < 0);
	int numPositions = QUICKKEYS_NUM_CHOICES + (hasCustomOriginal ? 1 : 0);
	int choice = quickkeyChoiceIndex(slotValues[slot]);
	int position = ((choice < 0) ? 0 : (choice + (hasCustomOriginal ? 1 : 0)));

	position = (position + (forward ? 1 : (numPositions - 1))) % numPositions;

	if (hasCustomOriginal)
	{
		slotValues[slot] = ((position == 0) ? slotOriginalValues[slot] : quickkeyChoices[position - 1]);
	}
	else
	{
		slotValues[slot] = quickkeyChoices[position];
	}
}

static bool quickkeysAreDirty(void)
{
	for (int i = 0; i < QUICKKEYS_NUM_SLOTS; i++)
	{
		if (slotValues[i] != slotOriginalValues[i])
		{
			return true;
		}
	}

	return false;
}

static bool quickkeysStore(void)
{
	bool ok = true;

	for (int i = 0; i < QUICKKEYS_NUM_SLOTS; i++)
	{
		if (slotValues[i] != slotOriginalValues[i])
		{
			char key = ('0' + i);

			// codeplugSetQuickkeyFunctionID() only writes into empty slots, so clear first.
			if (codeplugSetQuickkeyFunctionID(key, QUICKKEY_EMPTY) == false)
			{
				ok = false;
				continue;
			}

			if ((quickkeyValueIsEmpty(slotValues[i]) == false) && (codeplugSetQuickkeyFunctionID(key, slotValues[i]) == false))
			{
				ok = false;
			}
		}
	}

	return ok;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

menuStatus_t menuQuickkeys(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		for (int i = 0; i < QUICKKEYS_NUM_SLOTS; i++)
		{
			uint16_t value = codeplugGetQuickkeyFunctionID('0' + i);

			slotValues[i] = slotOriginalValues[i] = (quickkeyValueIsEmpty(value) ? QUICKKEY_EMPTY : value);
		}

		menuDataGlobal.numItems = QUICKKEYS_NUM_SLOTS;
		menuDataGlobal.currentItemIndex = 0;

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->quickkeys);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuQuickkeysExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}

	return menuQuickkeysExitCode;
}

static void updateScreen(bool isFirstRun)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char desc[SCREEN_LINE_BUFFER_SIZE];
	int mNum;

	displayClearBuf();
	menuDisplayTitle(currentLanguage->quickkeys);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(QUICKKEYS_NUM_SLOTS, i);
		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		quickkeyDescribe(slotValues[mNum], desc, sizeof(desc));
		// "5*APRS beacon": '*' flags a slot changed but not stored yet.
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%d%c%s", mNum, ((slotValues[mNum] != slotOriginalValues[mNum]) ? '*' : ' '), desc);

		menuDisplayEntry(i, mNum, buf, 2, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);

		if (i == 0)
		{
			if (isFirstRun == false)
			{
				voicePromptsInit();
			}

			voicePromptsAppendPrompt(PROMPT_0 + mNum);
			voicePromptsAppendString(desc);
			promptsPlayNotAfterTx();
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
		return;
	}

	if ((ev->events & KEY_EVENT) == 0)
	{
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, QUICKKEYS_NUM_SLOTS);
		updateScreen(false);
		menuQuickkeysExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, QUICKKEYS_NUM_SLOTS);
		updateScreen(false);
		menuQuickkeysExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT)
#endif
	)
	{
		quickkeyCycle(menuDataGlobal.currentItemIndex, true);
		updateScreen(false);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#endif
	)
	{
		quickkeyCycle(menuDataGlobal.currentItemIndex, false);
		updateScreen(false);
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
	{
		// Jump straight to a slot with its digit.
		menuDataGlobal.currentItemIndex = (ev->keys.key - '0');
		updateScreen(false);
		menuQuickkeysExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (quickkeysAreDirty())
		{
			bool ok = quickkeysStore();

			nextKeyBeepMelody = (int16_t *)(ok ? MELODY_ACK_BEEP : MELODY_ERROR_BEEP);
		}

		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
}
