/*
 * Fork: full screen text editor for the APRS texts (SP7TEAM firmware, Marcin SP7TQ).
 *
 * Uses the multi-tap keypad entry of the firmware (keypadAlphaEnable, as for the
 * contact / channel names): digit keys cycle through "abc2ABC" etc., '1' gives
 * punctuation, '0' digit / space. Insert mode: LEFT / RIGHT move the cursor
 * (long: start / end), UP / DOWN move by a line, SK2 + LEFT or '#' delete the
 * char before the cursor, SK2 + RIGHT inserts a space, GREEN accepts, RED cancels.
 *
 * The text buffer is owned by the caller (one screen at a time uses the editor).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_UI_TEXT_EDIT_H_
#define _OPENGD77_UI_TEXT_EDIT_H_

#include "user_interface/menuSystem.h"

typedef enum
{
	TEXT_EDIT_NONE = 0,  // event ignored
	TEXT_EDIT_CHANGED,   // redraw
	TEXT_EDIT_DONE,      // GREEN: the caller checks / uses the text
	TEXT_EDIT_CANCEL     // RED
} textEditResult_t;

// 'buf' (maxLen + 1 bytes) holds the initial text. 'callsign': A-Z, 0-9, '-' only (upper case).
void uiTextEditStart(char *buf, uint8_t maxLen, bool callsign);
textEditResult_t uiTextEditHandleEvent(uiEvent_t *ev);
void uiTextEditStop(void);
// Whole screen: title, optional label line ("TO SP5ABC"), the text, the key bar.
void uiTextEditDraw(const char *title, const char *label);

#endif
