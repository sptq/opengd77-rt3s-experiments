/*
 * Fork: PACE communication plan (main menu > PACE), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Four channels, as in military / rescue comms plans:
 *   P Primary, A Alternate, C Contingency, E Emergency.
 * '1'..'4' or GREEN switch the radio to that channel (channel mode), '*' stores the
 * current channel in the selected slot, '0' clears it, UP / DOWN select, RED leaves.
 * The plan is kept in the SPI flash (functions/fieldData.c).
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
#include "functions/fieldData.h"

#if defined(HAS_FIELD_TOOLS)

#define LIST_Y     (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H   11
#define ROW_H      ((DISPLAY_SIZE_Y - LIST_Y - FOOTER_H - 1) / PACE_SLOTS)
#define CHIP_W     20

static const char paceLetters[PACE_SLOTS] = { 'P', 'A', 'C', 'E' };
static const char *const paceNames[PACE_SLOTS] = { "PRIMARY", "ALTERNATE", "CONTINGENCY", "EMERGENCY" };
static int selectedSlot = 0;

static themeItem_t slotColour(int slot)
{
	switch (slot)
	{
		case 0:  return THEME_ITEM_FG_RSSI_BAR;      // green
		case 1:  return THEME_ITEM_FG_RX_FREQ;       // blue
		case 2:  return THEME_ITEM_FG_RSSI_BAR_S9P;  // amber
		default: return THEME_ITEM_FG_TX_COUNTER;    // red
	}
}

static bool onChannelScreen(void)
{
	return (menuDataGlobal.controlData.stack[0] == UI_CHANNEL_MODE);
}

static bool slotIsCurrent(const paceSlot_t *s)
{
	return (onChannelScreen() && (s->zoneNumber == nonVolatileSettings.currentZone) &&
			(s->channelNumber == uiDataGlobal.currentSelectedChannelNumber));
}

static void formatFreq(uint32_t freq, char *buf, size_t size)
{
	snprintf(buf, size, "%u.%04u", (unsigned)(freq / 100000U), (unsigned)((freq % 100000U) / 10U));
}

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];

	displayClearBuf();
	menuDisplayTitle("PACE PLAN");

	for (int i = 0; i < PACE_SLOTS; i++)
	{
		paceSlot_t slot;
		int16_t y = (int16_t)(LIST_Y + (i * ROW_H));
		bool sel = (i == selectedSlot);
		bool used = fieldPaceGet(i, &slot);

		// slot letter chip
		displayThemeApply(slotColour(i), THEME_ITEM_BG);
		displayFillRect(0, y, CHIP_W, (ROW_H - 2), false);
		buf[0] = paceLetters[i];
		buf[1] = 0;
		displayPrintCore(((CHIP_W - 8) / 2), (y + ((ROW_H - 2 - FONT_SIZE_3_HEIGHT) / 2)), buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);

		if (sel)
		{
			displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
			displayDrawRect((CHIP_W + 1), y, (DISPLAY_SIZE_X - CHIP_W - 1), (ROW_H - 2), true);
		}

		if (used && codeplugAllChannelsIndexIsInUse(slot.channelNumber))
		{
			CodeplugChannel_t ch;
			char name[17];

			codeplugChannelGetDataForIndex(slot.channelNumber, &ch);
			codeplugUtilConvertBufToString(ch.name, name, 16);

			displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
			displayPrintCore((CHIP_W + 4), (y + 1), name, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);

			formatFreq(ch.rxFreq, buf, sizeof(buf));
			strcat(buf, ((ch.chMode == RADIO_MODE_DIGITAL) ? " DMR" : " FM"));
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayPrintCore((CHIP_W + 4), (y + 11), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

			if (slotIsCurrent(&slot))
			{
				// "ON" chip: the radio is on this channel
				displayThemeApply(slotColour(i), THEME_ITEM_BG);
				displayFillRect((DISPLAY_SIZE_X - 21), (y + 2), 18, 9, false);
				displayPrintCore((DISPLAY_SIZE_X - 18), (y + 3), "ON", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
			}
		}
		else
		{
			displayThemeApply(slotColour(i), THEME_ITEM_BG);
			displayPrintCore((CHIP_W + 4), (y + 1), paceNames[i], FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayPrintCore((CHIP_W + 4), (y + 11), (used ? "channel deleted" : "empty - * stores"), FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 2), (onChannelScreen() ? "1-4 GO  * STORE  0 CLEAR" : "1-4 GO  (VFO: no STORE)"), FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeResetToDefault();
	displayRender();
}

// Switch the radio to the channel of a slot (channel mode). Returns false when the slot is empty.
static bool activateSlot(int i)
{
	paceSlot_t slot;
	CodeplugZone_t zone;
	int zones = codeplugZonesGetCount();
	int zoneNumber;
	int position = -1; // index in the zone, -2: the All Channels zone

	if ((fieldPaceGet(i, &slot) == false) || (codeplugAllChannelsIndexIsInUse(slot.channelNumber) == false))
	{
		return false;
	}

	zoneNumber = slot.zoneNumber;
	if ((zoneNumber < zones) && codeplugZoneGetDataForNumber(zoneNumber, &zone))
	{
		if (zone.NOT_IN_CODEPLUGDATA_indexNumber < 0)
		{
			position = -2;
		}
		else
		{
			for (int c = 0; c < zone.NOT_IN_CODEPLUGDATA_numChannelsInZone; c++)
			{
				if (zone.channels[c] == slot.channelNumber)
				{
					position = c;
					break;
				}
			}
		}
	}

	if (position == -1) // the zone changed in the CPS: use the All Channels zone
	{
		zoneNumber = (zones - 1);
		position = -2;
	}

	settingsSet(nonVolatileSettings.overrideTG, 0);
	settingsSet(nonVolatileSettings.currentZone, (int16_t)zoneNumber);
	settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 0);
	if (position == -2)
	{
		codeplugSetLastUsedChannelInZone(-1, slot.channelNumber);
	}
	else
	{
		codeplugSetLastUsedChannelInZone(zone.NOT_IN_CODEPLUGDATA_indexNumber, (int16_t)position);
	}
	channelScreenChannelData.rxFreq = 0x00; // reload the channel
	settingsSaveIfNeeded(true);

	menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, true);
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	return true;
}

static void storeCurrent(void)
{
	paceSlot_t slot;

	if (onChannelScreen() == false)
	{
		soundSetMelody(MELODY_ERROR_BEEP);
		return;
	}

	slot.zoneNumber = nonVolatileSettings.currentZone;
	slot.channelNumber = (int16_t)uiDataGlobal.currentSelectedChannelNumber;
	fieldPaceSet(selectedSlot, &slot);
	soundSetMelody(MELODY_ACK_BEEP);
}

menuStatus_t menuPACE(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
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
			if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if (activateSlot(selectedSlot) == false)
				{
					soundSetMelody(MELODY_ERROR_BEEP);
				}
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (ev->keys.key >= '1') && (ev->keys.key <= ('0' + PACE_SLOTS)))
			{
				selectedSlot = (ev->keys.key - '1');
				if (activateSlot(selectedSlot) == false)
				{
					soundSetMelody(MELODY_ERROR_BEEP);
					updateScreen();
				}
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
			{
				storeCurrent();
				updateScreen();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				fieldPaceSet(selectedSlot, NULL);
				soundSetMelody(MELODY_ACK_BEEP);
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
			{
				selectedSlot = ((selectedSlot + 1) % PACE_SLOTS);
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
			{
				selectedSlot = ((selectedSlot + PACE_SLOTS - 1) % PACE_SLOTS);
				updateScreen();
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuPACE(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
