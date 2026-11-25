/*
 * Fork: PACE communication plan (main menu > PACE), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Four channels, as in military / rescue comms plans:
 *   P Primary, A Alternate, C Contingency, E Emergency.
 * '1'..'4' or GREEN switch the radio to that channel (channel mode), '*' stores the
 * current channel in the selected slot, '0' clears it, '#' opens the comms schedule
 * (skeds, menuSked.c), UP / DOWN select, RED leaves. The next sked is counted down in the
 * EMCOM strip / the classic key bar.
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
#include "functions/sked.h"
#include "user_interface/uiEmcom.h"

#if defined(HAS_FIELD_TOOLS)

#define LIST_Y     (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H   11
#define ROW_H      ((DISPLAY_SIZE_Y - LIST_Y - FOOTER_H - 1) / PACE_SLOTS)
#define CHIP_W     20

static const char paceLetters[PACE_SLOTS] = { 'P', 'A', 'C', 'E' };
static const char *const paceNames[PACE_SLOTS] = { "PRIMARY", "ALTERNATE", "CONTINGENCY", "EMERGENCY" };
static int selectedSlot = 0;
static bool confirmClear = false; // '0' pressed: GREEN clears the selected slot

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

// The second drawn last (the sked count-down), in the CCM scratch: no static RAM
#define paceDrawnSecs (fieldToolsScratch[0])

// "S2 12:34" (next start in mm:ss), "S2 NOW", "" (no sked / no clock)
static bool skedText(char *buf, size_t size)
{
	skedNext_t next;
	char cd[12];

	skedNextGet(&next);
	if (next.slot < 0)
	{
		buf[0] = 0;
		return false;
	}

	if (next.on)
	{
		snprintf(buf, size, "S%d NOW", (next.slot + 1));
	}
	else
	{
		skedFormatCountdown(next.secs, cd, sizeof(cd));
		snprintf(buf, size, "S%d %s", (next.slot + 1), cd);
	}
	return true;
}

#if defined(HAS_EMCOM_UI)
#if defined(EMCOM_UI_MONO)
// 128 x 64 mono: title with the next sked (or the slots set), one 9 px row per slot, the selected
// slot as an 18 px inverse row with the frequency, mode and slot name, the key bar or the confirmation
#define MONO_ROW_H     9
#define MONO_SEL_H     18
#define MONO_TEXT_X    12

static void updateScreenMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int16_t y = 10;
	int set = 0;

	displayClearBuf();

	for (int i = 0; i < PACE_SLOTS; i++)
	{
		paceSlot_t slot;
		bool sel = (i == selectedSlot);
		bool used = fieldPaceGet(i, &slot);
		bool exists = (used && codeplugAllChannelsIndexIsInUse(slot.channelNumber));
		const char *state = NULL; // the chip on the right: ON / DELETED / EMPTY
		char name[17];
		CodeplugChannel_t ch;
		int16_t chipW = 0;
		size_t maxChars;

		set += (used ? 1 : 0);

		if (exists)
		{
			codeplugChannelGetDataForIndex(slot.channelNumber, &ch);
			codeplugUtilConvertBufToString(ch.name, name, 16);
			if (slotIsCurrent(&slot))
			{
				state = "ON";
			}
		}
		else
		{
			snprintf(name, sizeof(name), "%s", paceNames[i]);
			state = (used ? "DELETED" : "EMPTY");
		}

		if (state != NULL)
		{
			chipW = (int16_t)((strlen(state) * 6) + 3);
		}
		maxChars = (size_t)((DISPLAY_SIZE_X - 1 - chipW - 3 - MONO_TEXT_X) / 6);
		if (strlen(name) > maxChars)
		{
			name[maxChars] = 0;
		}

		if (sel)
		{
			displayFillRect(0, y, DISPLAY_SIZE_X, MONO_SEL_H, false);
			buf[0] = paceLetters[i];
			buf[1] = 0;
			displayPrintCore(1, (y + 1), buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
			displayPrintCore(MONO_TEXT_X, (y + 1), name, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
			if (state != NULL)
			{
				// light chip on the inverse row
				int16_t cx = (int16_t)(DISPLAY_SIZE_X - 1 - chipW);

				displayFillRect(cx, y, chipW, 9, true);
				displayPrintCore((cx + 2), (y + 1), state, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
			}

			if (exists)
			{
				formatFreq(ch.rxFreq, buf, sizeof(buf));
				strcat(buf, ((ch.chMode == RADIO_MODE_DIGITAL) ? " DMR" : " FM"));
				displayPrintCore(MONO_TEXT_X, (y + 9), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
				if ((MONO_TEXT_X + (int16_t)((strlen(buf) + 1 + strlen(paceNames[i])) * 6)) <= (DISPLAY_SIZE_X - 1))
				{
					displayPrintCore((int16_t)(DISPLAY_SIZE_X - 1 - (strlen(paceNames[i]) * 6)), (y + 9), paceNames[i], FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
				}
			}
			else
			{
				displayPrintCore(MONO_TEXT_X, (y + 9), (used ? "channel deleted" : "* stores channel"), FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
			}
			y += MONO_SEL_H;
		}
		else
		{
			displayFillRect(0, (y + 1), 9, 8, false);
			buf[0] = paceLetters[i];
			buf[1] = 0;
			displayPrintCore(2, (y + 1), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
			emcomUiText(MONO_TEXT_X, (y + 1), name, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
			if (state != NULL)
			{
				emcomUiChip((DISPLAY_SIZE_X - 1), (y + 1), state, THEME_ITEM_FG_DEFAULT, (exists ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), true);
			}
			y += MONO_ROW_H;
		}
	}

	// title: the next sked count-down when one is set, else the slots set
	char sked[16];

	if (skedText(sked, sizeof(sked)) == false)
	{
		snprintf(sked, sizeof(sked), "%d/%d SET", set, PACE_SLOTS);
	}
	emcomUiTitle("PACE PLAN", sked);

	if (confirmClear)
	{
		snprintf(buf, sizeof(buf), "CLR %c?", paceLetters[selectedSlot]);
		emcomUiConfirmBar(buf);
	}
	else
	{
		emcomUiKeyBar(onChannelScreen() ? "1-4GO *STO 0CLR #SKED" : "1-4 GO  # SKED  (VFO)");
	}
	paceDrawnSecs = uiDataGlobal.dateTimeSecs;
	displayRender();
}
#else
// EMCOM style: status strip (the slot the radio is on), one two-line row per slot with its state
// (ON / EMPTY / DELETED), the key bar or the clear confirmation
#define EM_ROW_H    ((EMCOM_UI_KEYBAR_Y - EMCOM_UI_BODY_Y + 1) / PACE_SLOTS)
#define EM_TEXT_X   (CHIP_W + 4)

static void updateScreenEmcom(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int active = -1;
	int set = 0;

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%d/%d", (selectedSlot + 1), PACE_SLOTS);
	emcomUiTitle("PACE PLAN", buf);

	for (int i = 0; i < PACE_SLOTS; i++)
	{
		paceSlot_t slot;
		int16_t y = (int16_t)(EMCOM_UI_BODY_Y + (i * EM_ROW_H));
		bool sel = (i == selectedSlot);
		bool used = fieldPaceGet(i, &slot);
		bool exists = (used && codeplugAllChannelsIndexIsInUse(slot.channelNumber));
		themeItem_t text = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT);
		themeItem_t muted = (sel ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION);

		set += (used ? 1 : 0);

		// slot letter block, the selected row as a bar beside it
		displayThemeApply(slotColour(i), THEME_ITEM_BG);
		displayFillRect(0, y, CHIP_W, (EM_ROW_H - 1), false);
		buf[0] = paceLetters[i];
		buf[1] = 0;
		displayPrintCore(((CHIP_W - 8) / 2), (y + ((EM_ROW_H - 1 - FONT_SIZE_3_HEIGHT) / 2)), buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
		if (sel)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect((CHIP_W + 1), y, (DISPLAY_SIZE_X - CHIP_W - 1), (EM_ROW_H - 1), false);
		}
		else if (i < (PACE_SLOTS - 1))
		{
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayDrawFastHLine((CHIP_W + 1), (y + EM_ROW_H - 1), (DISPLAY_SIZE_X - CHIP_W - 1), true);
		}
		displayThemeResetToDefault();

		if (exists)
		{
			CodeplugChannel_t ch;
			char name[17];

			codeplugChannelGetDataForIndex(slot.channelNumber, &ch);
			codeplugUtilConvertBufToString(ch.name, name, 16);
			emcomUiText(EM_TEXT_X, (y + 3), name, FONT_SIZE_1_BOLD, text, false);

			formatFreq(ch.rxFreq, buf, sizeof(buf));
			strcat(buf, ((ch.chMode == RADIO_MODE_DIGITAL) ? " DMR" : " FM"));
			emcomUiText(EM_TEXT_X, (y + 12), buf, FONT_SIZE_1, muted, false);
			if ((EM_TEXT_X + (int16_t)((strlen(buf) + 1 + strlen(paceNames[i])) * 6)) <= (DISPLAY_SIZE_X - 2))
			{
				emcomUiText((DISPLAY_SIZE_X - 2), (y + 12), paceNames[i], FONT_SIZE_1, muted, true);
			}

			if (slotIsCurrent(&slot))
			{
				active = i;
				emcomUiChip((DISPLAY_SIZE_X - 2), (y + 3), "ON", (sel ? THEME_ITEM_BG : slotColour(i)), (sel ? EMCOM_CHIP_OUTLINE : EMCOM_CHIP_SOLID), true);
			}
		}
		else
		{
			emcomUiText(EM_TEXT_X, (y + 3), paceNames[i], FONT_SIZE_1_BOLD, (sel ? THEME_ITEM_BG : slotColour(i)), false);
			emcomUiText(EM_TEXT_X, (y + 12), (used ? "channel deleted" : "* stores the channel"), FONT_SIZE_1, muted, false);
			emcomUiChip((DISPLAY_SIZE_X - 2), (y + 3), (used ? "DELETED" : "EMPTY"), (sel ? THEME_ITEM_BG : (used ? THEME_ITEM_FG_TX_COUNTER : THEME_ITEM_FG_DECORATION)),
					EMCOM_CHIP_OUTLINE, true);
		}
	}

	// status strip: the slot the radio is on now
	emcomUiStrip();
	// right: the next sked count-down when one is set, else the slots set
	char sked[16];
	int16_t rightW;

	if (skedText(sked, sizeof(sked)))
	{
		rightW = emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), sked, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, true);
		rightW = (int16_t)(rightW + emcomUiText((DISPLAY_SIZE_X - 3 - rightW - 4), (EMCOM_UI_STRIP_Y + 2), "SKED", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true) + 4);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%d/%d SET", set, PACE_SLOTS);
		rightW = emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
	}

	if (active >= 0)
	{
		snprintf(buf, sizeof(buf), "ON %c", paceLetters[active]);
		int16_t x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), buf, slotColour(active), EMCOM_CHIP_SOLID, false) + 4);
		if ((x + (int16_t)(strlen(paceNames[active]) * 6) + 10) <= (DISPLAY_SIZE_X - 3 - rightW)) // the slot name when it fits, clear of the right text
		{
			emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), paceNames[active], FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
		}
	}
	else
	{
		emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), (onChannelScreen() ? "OFF PLAN" : "VFO"), (onChannelScreen() ? THEME_ITEM_FG_DECORATION : THEME_ITEM_FG_RSSI_BAR_S9P),
				EMCOM_CHIP_OUTLINE, false);
	}

	if (confirmClear)
	{
		snprintf(buf, sizeof(buf), "CLEAR %c?", paceLetters[selectedSlot]);
		emcomUiConfirmBar(buf);
	}
	else
	{
		emcomUiKeyBar(onChannelScreen() ? "1-4 GO * STO 0 CLR # SKED" : "1-4 GO  # SKED  (VFO)");
	}
	paceDrawnSecs = uiDataGlobal.dateTimeSecs;
	displayRender();
}
#endif // EMCOM_UI_MONO
#endif

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];

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
	if (confirmClear)
	{
		snprintf(buf, sizeof(buf), "CLEAR %c?", paceLetters[selectedSlot]);
		displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 2), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(57, (fy + 2), "GREEN = yes", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else
	{
		char sked[16];

		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		if (skedText(sked, sizeof(sked)))
		{
			// the next sked replaces the key hints (# opens the schedule)
			snprintf(buf, sizeof(buf), "%s  # SKED %s", (onChannelScreen() ? "* STORE" : "1-4 GO"), sked);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%s", (onChannelScreen() ? "1-4 GO * STO 0 CLR # SKED" : "1-4 GO  # SKED  (VFO)"));
		}
		displayPrintCore(3, (fy + 2), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();
	paceDrawnSecs = uiDataGlobal.dateTimeSecs;
	displayRender();
}

// Switch the radio to a zone / channel (channel mode). Returns false when the channel is deleted.
// Also used by the sked "NOW" card (functions/sked.c).
bool paceActivateChannel(const paceSlot_t *data)
{
	paceSlot_t slot = *data;
	CodeplugZone_t zone;
	int zones = codeplugZonesGetCount();
	int zoneNumber;
	int position = -1; // index in the zone, -2: the All Channels zone

	if ((slot.zoneNumber < 0) || (codeplugAllChannelsIndexIsInUse(slot.channelNumber) == false))
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

// Switch the radio to the channel of a slot (channel mode). Returns false when the slot is empty.
static bool activateSlot(int i)
{
	paceSlot_t slot;

	return (fieldPaceGet(i, &slot) && paceActivateChannel(&slot));
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
		confirmClear = false;
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
				// decided on the key release (the press of GREEN would cancel); RED cancels, stays here
				if (ev->keys.event & KEY_MOD_UP)
				{
					if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
					{
						fieldPaceSet(selectedSlot, NULL);
						soundSetMelody(MELODY_ACK_BEEP);
					}
					confirmClear = false;
					updateScreen();
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
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
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				menuSystemPushNewMenu(MENU_SKED);
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				paceSlot_t slot;

				if (fieldPaceGet(selectedSlot, &slot))
				{
					confirmClear = true;
				}
				else
				{
					soundSetMelody(MELODY_ERROR_BEEP); // already empty
				}
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

	// the sked count-down, once per second (only reads the flash when the clock ticked)
	if ((confirmClear == false) && (paceDrawnSecs != uiDataGlobal.dateTimeSecs) && skedClockValid())
	{
		updateScreen();
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
