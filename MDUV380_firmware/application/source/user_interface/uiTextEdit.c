/*
 * Fork: full screen text editor for the APRS texts (see user_interface/uiTextEdit.h).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiTextEdit.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "functions/aprsText.h"
#include "io/keyboard.h"

#if !defined(PLATFORM_GD77S)

#define EDIT_X          4
#define EDIT_PER_LINE   ((DISPLAY_SIZE_X - (2 * EDIT_X)) / 8) // FONT_SIZE_3: 8 px wide
#define EDIT_FOOTER_H   10

// CCM (main RAM is full); not zeroed at boot, every field is set by uiTextEditStart()
#define EDIT_CCMRAM  __attribute__((section(".ccmram")))
static EDIT_CCMRAM char   *editBuf;
static EDIT_CCMRAM uint8_t editMax;
static EDIT_CCMRAM uint8_t editPos;
static EDIT_CCMRAM bool    editCallsign;
static EDIT_CCMRAM bool    editPreview;  // the char at editPos is a multi-tap preview
static EDIT_CCMRAM bool    editActive;

static void editInsert(char c)
{
	uint32_t len = strlen(editBuf);

	memmove(&editBuf[editPos + 1], &editBuf[editPos], ((len - editPos) + 1)); // with the NUL
	editBuf[editPos] = c;
}

static void editDelete(uint8_t pos)
{
	uint32_t len = strlen(editBuf);

	if (pos < len)
	{
		memmove(&editBuf[pos], &editBuf[pos + 1], (len - pos)); // with the NUL
	}
}

void uiTextEditStart(char *buf, uint8_t maxLen, bool callsign)
{
	editBuf = buf;
	editMax = maxLen;
	editBuf[maxLen] = 0;
	editPos = (uint8_t)strlen(buf);
	editCallsign = callsign;
	editPreview = false;
	editActive = true;
	keypadAlphaEnable = true;
}

void uiTextEditStop(void)
{
	keypadAlphaEnable = false;
	editPreview = false;
	editActive = false;
}

// keyboardReset() (APRS transmissions: beacons, acks, message retries; SK2 + long digit, ...)
// turns the multi-tap entry off: turn it on again (as menuContactDetails.c does on each
// redraw); a char that was being cycled stays as it is.
static void editKeepAlpha(void)
{
	if (editActive && (keypadAlphaEnable == false))
	{
		editPreview = false;
		keypadAlphaEnable = true;
	}
}

textEditResult_t uiTextEditHandleEvent(uiEvent_t *ev)
{
	uint8_t len;

	editKeepAlpha();

	if ((ev->events & KEY_EVENT) == 0)
	{
		return TEXT_EDIT_NONE;
	}

	len = (uint8_t)strlen(editBuf);

	// Multi-tap chars (keyboard.c): PREVIEW while the key cycles, PRESS (alone) when accepted
	if ((ev->keys.key >= ' ') && ((ev->keys.event == KEY_MOD_PREVIEW) || (ev->keys.event == KEY_MOD_PRESS)))
	{
		bool accepted = (ev->keys.event == KEY_MOD_PRESS);
		char c = aprsTextNormaliseChar((char)ev->keys.key, editCallsign);

		if (editPreview)
		{
			if (c)
			{
				editBuf[editPos] = c;
			}
			else
			{
				editDelete(editPos); // e.g. '|' while cycling the '1' key: not allowed
				editPreview = false;
			}
		}
		else if (c && (len < editMax))
		{
			editInsert(c);
			editPreview = true;
		}

		if (editPreview && accepted)
		{
			editPos++;
			editPreview = false;
			announceChar(c);
		}
		return TEXT_EDIT_CHANGED;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		uiTextEditStop();
		return TEXT_EDIT_DONE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		uiTextEditStop();
		return TEXT_EDIT_CANCEL;
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_LEFT))
	{
		editPos = 0;
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RIGHT))
	{
		editPos = len;
	}
	else if ((KEYCHECK_SHORTUP(ev->keys, KEY_LEFT) && BUTTONCHECK_DOWN(ev, BUTTON_SK2)) || KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		if (editPos > 0)
		{
			editPos--;
			announceChar(editBuf[editPos]);
			editDelete(editPos);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		if (editPos > 0)
		{
			editPos--;
			announceChar(editBuf[editPos]);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		if ((editCallsign == false) && (len < editMax))
		{
			editInsert(' ');
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		if (editPos < len)
		{
			editPos++;
			announceChar(editBuf[editPos]);
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		editPos = ((editPos > EDIT_PER_LINE) ? (editPos - EDIT_PER_LINE) : 0);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		editPos = (((editPos + EDIT_PER_LINE) < len) ? (editPos + EDIT_PER_LINE) : len);
	}
	else
	{
		return TEXT_EDIT_NONE;
	}

	return TEXT_EDIT_CHANGED;
}

void uiTextEditDraw(const char *title, const char *label)
{
	char line[EDIT_PER_LINE + 1];
	int32_t savedNumItems = menuDataGlobal.numItems;
	uint32_t len = strlen(editBuf);
	int16_t y = (MENU_HEADER_HEIGHT + 2);

	editKeepAlpha();
	displayClearBuf();

	menuDataGlobal.numItems = 0; // no list position in the title bar: the length goes there
	menuDisplayTitle(title);
	menuDataGlobal.numItems = savedNumItems;

	snprintf(line, sizeof(line), "%u/%u", (unsigned)len, (unsigned)editMax);
	displayThemeApply(((len >= editMax) ? THEME_ITEM_FG_TX_COUNTER : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG_MENU_NAME);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(line) * 6)), 4, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	if (label)
	{
		displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
		displayPrintCore(EDIT_X, y, label, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		y += (FONT_SIZE_1_HEIGHT + 2);
	}

	// Text box, scrolled to keep the cursor visible
	uint32_t lines = ((DISPLAY_SIZE_Y - EDIT_FOOTER_H - 4 - y) / FONT_SIZE_3_HEIGHT);
	uint32_t cursorLine = (editPos / EDIT_PER_LINE);
	uint32_t first = ((cursorLine >= lines) ? (cursorLine - lines + 1) : 0);

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawRect(0, y, DISPLAY_SIZE_X, (int16_t)((lines * FONT_SIZE_3_HEIGHT) + 2), true);

	for (uint32_t l = 0; l < lines; l++)
	{
		uint32_t start = ((first + l) * EDIT_PER_LINE);
		int16_t ly = (int16_t)(y + 1 + (l * FONT_SIZE_3_HEIGHT));

		line[0] = 0;
		if (start < len)
		{
			uint32_t n = (((len - start) > EDIT_PER_LINE) ? EDIT_PER_LINE : (len - start));

			memcpy(line, &editBuf[start], n);
			line[n] = 0;
		}
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(EDIT_X, ly, line, FONT_SIZE_3, TEXT_ALIGN_LEFT, false);

		if ((first + l) == cursorLine)
		{
			int16_t cx = (int16_t)(EDIT_X + ((editPos % EDIT_PER_LINE) * 8));

			if (editPreview)
			{
				// char being chosen: inverted
				char c[2] = { editBuf[editPos], 0 };

				displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
				displayFillRect(cx, ly, 8, FONT_SIZE_3_HEIGHT, false);
				displayPrintCore(cx, ly, c, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
			}
			else
			{
				displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
				displayFillRect(cx, (ly + FONT_SIZE_3_HEIGHT - 2), 8, 2, false);
			}
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - EDIT_FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, EDIT_FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 1), "GRN OK  # DEL  RED BACK", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeResetToDefault();
	displayRender();
}

#endif
