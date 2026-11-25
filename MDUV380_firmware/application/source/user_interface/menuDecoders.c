/*
 * Fork: Decoders screen (main menu > Decoders), by Marcin SP7TQ. The tone decoders that run on
 * the FM RX audio next to the APRS decoder (functions/aprsRx.c, toneRxDecoders.c), one view each,
 * cycled with '#':
 *   DTMF  decoded digits (large font), sequences also go to the event log
 *   CW    decoded text, speed and tone
 *   STQC  Digitex / Platan selcalls (receive only): address / command and the tones as sent
 * '0' clears the current view. The capture runs while this screen is open (as on APRS RX) and,
 * with APRS RX in the background, all the time.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "interfaces/i2s.h"
#include "functions/aprsRx.h"
#include "functions/toneRxDecoders.h"
#include "functions/ticks.h"

#if defined(HAS_APRS_RX_MENU) && defined(I2S_APRS_RX)

#define DEC_REDRAW_MS     1000U
#define DEC_CHARS         26     // 160 px / 6 px font
#define DEC_STATUS_Y      (MENU_HEADER_HEIGHT + 2)
#define DEC_LIST_Y        (DEC_STATUS_Y + FONT_SIZE_1_HEIGHT + 3)
#define DEC_FOOTER_H      10
#define DEC_STQC_BLOCK_H  ((2 * FONT_SIZE_1_HEIGHT) + 2)
#define DEC_STQC_BLOCKS   ((DISPLAY_SIZE_Y - DEC_FOOTER_H - DEC_LIST_Y) / DEC_STQC_BLOCK_H)

typedef enum
{
	DEC_VIEW_DTMF = 0,
	DEC_VIEW_CW,
	DEC_VIEW_STQC,
	DEC_VIEW_MAX
} decView_t;

static decView_t decView = DEC_VIEW_DTMF;
static uint32_t decLastDraw;
static uint32_t decToneSerial;
static uint32_t decStqcSerial;

static int16_t decChip(int16_t x, int16_t y, const char *text, themeItem_t colour)
{
	int16_t w = (int16_t)((strlen(text) * 6) + 4);

	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillRect(x, (y - 1), w, (FONT_SIZE_1_HEIGHT + 1), false);
	displayPrintCore((x + 2), y, text, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	displayThemeResetToDefault();
	return w;
}

static void decFooter(const char *text, const char *chip, themeItem_t chipColour)
{
	int16_t y = (DISPLAY_SIZE_Y - DEC_FOOTER_H);

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, y, DISPLAY_SIZE_X, DEC_FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (y + 1), text, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	if (chip)
	{
		decChip((DISPLAY_SIZE_X - 2 - (int16_t)((strlen(chip) * 6) + 4)), (y + 1), chip, chipColour);
	}
	displayThemeResetToDefault();
}

static void decAge(uint32_t ms, char *buf, size_t size)
{
	uint32_t s = (ms / 1000U);

	if (s < 60U)
	{
		snprintf(buf, size, "%us", (unsigned)s);
	}
	else if (s < 3600U)
	{
		snprintf(buf, size, "%um", (unsigned)(s / 60U));
	}
	else
	{
		snprintf(buf, size, "%uh", (unsigned)(s / 3600U));
	}
}

// Empty view: a heading and a hint (on a DMR channel: the radio must be on FM)
static void decHint(const char *line1, const char *line2)
{
	if (trxGetMode() != RADIO_MODE_ANALOG)
	{
		line1 = "FM only";
		line2 = "Switch to an FM channel";
	}

	displayThemeApply(((trxGetMode() != RADIO_MODE_ANALOG) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_DEFAULT), THEME_ITEM_BG);
	displayPrintCentered((DEC_LIST_Y + 22), line1, FONT_SIZE_3);
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	displayPrintCentered((DEC_LIST_Y + 44), line2, FONT_SIZE_1);
	displayThemeResetToDefault();
}

// --- DTMF / CW -------------------------------------------------------------------
static void decDrawToneText(bool cw)
{
	char text[APRS_RX_TONE_TEXT_LEN + 1];
	char line[24];
	uint32_t perLine = (DISPLAY_SIZE_X / 8); // FONT_SIZE_3: 8 px wide
	uint32_t lines = ((DISPLAY_SIZE_Y - DEC_LIST_Y - DEC_FOOTER_H - 2) / FONT_SIZE_3_HEIGHT);
	uint32_t len = aprsRxToneText(cw, text, sizeof(text), NULL);

	if (cw)
	{
		uint8_t wpm = cwRxGetWpm();

		if (wpm)
		{
			snprintf(line, sizeof(line), "%u WPM  %u Hz", (unsigned)wpm, (unsigned)cwRxGetToneHz());
		}
		else
		{
			snprintf(line, sizeof(line), "# VIEW  0 CLR  RED EXIT");
		}
		decFooter(line, (cwRxIsKeyed() ? "KEY" : NULL), THEME_ITEM_FG_RSSI_BAR);
	}
	else
	{
		decFooter("# VIEW  0 CLR  RED EXIT", NULL, THEME_ITEM_FG_DEFAULT);
	}

	if (len == 0)
	{
		decHint((cw ? "Waiting for CW" : "Waiting for DTMF"), "FM RX audio");
		return;
	}

	// Last 'lines' lines of text
	uint32_t maxChars = (perLine * lines);
	uint32_t start = ((len > maxChars) ? (len - maxChars) : 0);
	if (start)
	{
		start += ((perLine - ((len - start) % perLine)) % perLine); // keep line alignment stable
	}

	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	for (uint32_t l = 0; l < lines; l++)
	{
		uint32_t off = (start + (l * perLine));
		uint32_t n = 0;

		if (off >= len)
		{
			break;
		}
		while ((n < perLine) && ((off + n) < len))
		{
			line[n] = text[off + n];
			n++;
		}
		line[n] = 0;
		displayPrintAt(0, (DEC_LIST_Y + (l * FONT_SIZE_3_HEIGHT)), line, FONT_SIZE_3);
	}
	displayThemeResetToDefault();
}

// --- STQC: the last selcalls, newest first -----------------------------------------
static void decDrawStqc(void)
{
	char line[DEC_CHARS + 1];
	char age[8];
	uint32_t count = aprsRxStqcCount();
	uint32_t now = ticksGetMillis();

	decFooter("# VIEW  0 CLR  RED EXIT", NULL, THEME_ITEM_FG_DEFAULT);
	if (count == 0)
	{
		decHint("Waiting for STQC", "selcall, receive only");
		return;
	}

	for (uint32_t b = 0; (b < DEC_STQC_BLOCKS) && (b < count); b++)
	{
		const aprsRxStqcCall_t *call = aprsRxStqcGet(b);
		int16_t y = (DEC_LIST_Y + (b * DEC_STQC_BLOCK_H));
		char addr[STQC_MAX_SYMBOLS + 1];
		char cmd[STQC_MAX_SYMBOLS + 1];

		decAge((now - call->ms), age, sizeof(age));
		aprsRxStqcFormat(call, line, sizeof(line));
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(4, y, line, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(0, y, age, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);

		// the tones as sent (0..4, 4 = repeat), to compare with other decoders
		addr[0] = cmd[0] = 0;
		if (call->addrLen)
		{
			stqcValueToSymbolText(call->addr, call->addrLen, addr);
		}
		if (call->cmdLen)
		{
			stqcValueToSymbolText(call->cmd, call->cmdLen, cmd);
		}
		snprintf(line, sizeof(line), "%s%s%s", addr, ((addr[0] && cmd[0]) ? " " : ""), cmd);
		displayPrintCore(4, (y + FONT_SIZE_1_HEIGHT), line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		displayThemeResetToDefault();
	}
}

static void decDraw(void)
{
	static const char *titles[] = { "DTMF decoder", "CW decoder", "STQC selcall" };
	char line[DEC_CHARS + 1];
	aprsRxStatus_t st;
	int16_t sy = DEC_STATUS_Y;
	int16_t x = 2;

	aprsRxGetStatus(&st);

	displayClearBuf();
	menuDisplayTitle(titles[decView]);

	// Status strip: state chip, audio level, dBm
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DEC_LIST_Y - 2 - MENU_HEADER_HEIGHT), false);

	if (st.state == APRS_RX_STATE_LISTENING)
	{
		x += decChip(x, sy, "RX", THEME_ITEM_FG_RSSI_BAR) + 4;
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
		displayDrawRect(x, (sy + 1), 42, 6, true);
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG_HEADER_TEXT);
		displayFillRect((x + 1), (sy + 2), (int16_t)((st.levelPercent * 40U) / 100U), 4, false);
	}
	else
	{
		decChip(x, sy, ((trxGetMode() != RADIO_MODE_ANALOG) ? "FM ONLY" : "PAUSE"), THEME_ITEM_FG_RSSI_BAR_S9P);
	}

	snprintf(line, sizeof(line), "%ddBm", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(line) * 6)), sy, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, (DEC_LIST_Y - 2), DISPLAY_SIZE_X, true);

	// view position in the title bar
	snprintf(line, sizeof(line), "%d/%d", (int)(decView + 1), (int)DEC_VIEW_MAX);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_MENU_NAME);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(line) * 6)), 4, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();

	switch (decView)
	{
		case DEC_VIEW_CW:
			decDrawToneText(true);
			break;
		case DEC_VIEW_STQC:
			decDrawStqc();
			break;
		default:
			decDrawToneText(false);
			break;
	}

	displayRender();
}

static void decUpdate(uint32_t now)
{
	char tmp[2];
	uint32_t serial;

	if (decView == DEC_VIEW_STQC)
	{
		serial = aprsRxStqcSerial();
		if (serial != decStqcSerial)
		{
			decStqcSerial = serial;
			decLastDraw = 0;
		}
	}
	else
	{
		aprsRxToneText((decView == DEC_VIEW_CW), tmp, sizeof(tmp), &serial);
		if (serial != decToneSerial)
		{
			decToneSerial = serial;
			decLastDraw = 0;
		}
	}

	if ((decLastDraw == 0) || ((now - decLastDraw) >= DEC_REDRAW_MS))
	{
		decLastDraw = (now ? now : 1);
		decDraw();
	}
}

menuStatus_t menuDecoders(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		aprsRxSetMonitor(true);
		decLastDraw = 0;
		decDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			decDraw();
		}
		else if (ev->events & KEY_EVENT)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				aprsRxSetMonitor(false);
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				decView = (decView + 1) % DEC_VIEW_MAX;
				decLastDraw = 0;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				if (decView == DEC_VIEW_STQC)
				{
					aprsRxStqcClear();
				}
				else
				{
					aprsRxToneTextClear();
				}
				decLastDraw = 0;
			}
		}
	}

	if (menuSystemGetCurrentMenuNumber() == MENU_DECODERS) // still here (not left by a key above)
	{
		decUpdate(ev->time);
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuDecoders(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
