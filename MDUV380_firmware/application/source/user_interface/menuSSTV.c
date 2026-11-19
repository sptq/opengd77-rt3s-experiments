/*
 * Fork: SSTV picture receive screen (main menu > SSTV), by Marcin SP7TQ.
 *
 * Tune an FM channel / VFO (e.g. the ISS on 145.800 MHz) and open this
 * screen: the RX audio goes to the SSTV decoder (functions/sstvRx.c), the
 * mode comes from the VIS header and the picture (160 x 120) is drawn line by
 * line under a one line status bar. '0' clears and waits for the next VIS,
 * GREEN / RED leave. APRS / DTMF / CW decoding is paused meanwhile (the SSTV
 * decoder borrows their memory).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/sound.h"
#include "interfaces/i2s.h"
#include "functions/aprsRx.h"
#include "functions/sstvRx.h"
#include "functions/eventLog.h"
#include "functions/ticks.h"

#if defined(HAS_SSTV_MENU) && defined(I2S_APRS_RX)

#define SSTV_Y0          (DISPLAY_SIZE_Y - SSTV_RX_ROWS) // picture under a one line status bar
// Screens of only 120 lines (DM-1701): the status line covers the top of the picture
#define SSTV_STATUS_H    ((SSTV_Y0 >= FONT_SIZE_1_HEIGHT) ? SSTV_Y0 : FONT_SIZE_1_HEIGHT)
#define SSTV_REDRAW_MS   250U
#define SSTV_CHARS       26

static uint32_t lastDraw = 0;
static sstvRxState_t lastState = SSTV_RX_IDLE;
static bool hasPicture = false;

// IRQ context: the decoder writes the pixels straight into the screen buffer
static void sstvPixel(uint16_t x, uint16_t row, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t *fb = displayGetPrimaryScreenBuffer();

	fb[((SSTV_Y0 + row) * DISPLAY_SIZE_X) + x] = displayConvertRGB888ToNative(((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
}

// Coloured field with the text in the background colour; returns its width
static int16_t chip(int16_t x, int16_t y, int16_t h, const char *text, themeItem_t colour, themeItem_t bg)
{
	int16_t w = (int16_t)((strlen(text) * 6) + 4);

	displayThemeApply(colour, bg);
	displayFillRect(x, y, w, h, false);
	displayThemeApply(colour, THEME_ITEM_BG);
	displayPrintCore((x + 2), (y + ((h - FONT_SIZE_1_HEIGHT) / 2)), text, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	return w;
}

static const char *rxStateText(const aprsRxStatus_t *rx, themeItem_t *colour)
{
	if (rx->state == APRS_RX_STATE_LISTENING)
	{
		*colour = THEME_ITEM_FG_RSSI_BAR;
		return "RX";
	}
	if (trxGetMode() != RADIO_MODE_ANALOG)
	{
		*colour = THEME_ITEM_FG_RSSI_BAR_S9P;
		return "FM ONLY";
	}
	if (rx->state == APRS_RX_STATE_NO_CLOCK)
	{
		*colour = THEME_ITEM_FG_TX_COUNTER;
		return "NO CLK";
	}
	*colour = THEME_ITEM_FG_RSSI_BAR_S9P;
	return "PAUSE";
}

// No picture yet: the full layout of the other screens (title, status strip, hint, key bar)
static void drawIdle(const sstvRxStatus_t *st, const aprsRxStatus_t *rx)
{
	char line[SSTV_CHARS + 1];
	themeItem_t colour;
	const char *state = rxStateText(rx, &colour);
	int16_t sy = MENU_HEADER_HEIGHT;

	displayClearBuf();
	menuDisplayTitle("SSTV");

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, sy, DISPLAY_SIZE_X, 11, false);
	int16_t x = (int16_t)(2 + chip(2, (sy + 1), 9, state, colour, THEME_ITEM_BG_HEADER_TEXT) + 4);

	if (st->lastVis)
	{
		snprintf(line, sizeof(line), "VIS %u ?", (unsigned)st->lastVis); // not a known mode
	}
	else if (st->freqHz)
	{
		snprintf(line, sizeof(line), "%u Hz", (unsigned)st->freqHz);
	}
	else
	{
		line[0] = 0;
	}
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(x, (sy + 2), line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	snprintf(line, sizeof(line), "%ddBm", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(line) * 6)), (sy + 2), line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	if (trxGetMode() != RADIO_MODE_ANALOG)
	{
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		displayPrintCentered(44, "FM only", FONT_SIZE_3);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered(66, "Switch to an FM", FONT_SIZE_1);
		displayPrintCentered(76, "channel or VFO", FONT_SIZE_1);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered(36, "Waiting for", FONT_SIZE_3);
		displayPrintCentered(54, "SSTV picture", FONT_SIZE_3);
		displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
		displayPrintCentered(78, "e.g. ISS 145.800 FM", FONT_SIZE_1);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCentered(90, "ROBOT MARTIN SCOTTIE PD", FONT_SIZE_1);
	}

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, (DISPLAY_SIZE_Y - 11), DISPLAY_SIZE_X, 11, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (DISPLAY_SIZE_Y - 9), "0 CLEAR  RED EXIT", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
}

// Picture shown: compact bar above it (state field, mode, line / done)
static void drawPictureBar(const sstvRxStatus_t *st, const aprsRxStatus_t *rx)
{
	char line[SSTV_CHARS + 1];
	themeItem_t colour;
	const char *state = rxStateText(rx, &colour);

	if ((st->state == SSTV_RX_DONE) && (rx->state == APRS_RX_STATE_LISTENING))
	{
		state = "DONE";
		colour = THEME_ITEM_FG_RX_FREQ;
	}

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, 0, DISPLAY_SIZE_X, SSTV_STATUS_H, false);
	int16_t x = (int16_t)(chip(0, 0, SSTV_STATUS_H, state, colour, THEME_ITEM_BG_HEADER_TEXT) + 4);

	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(x, ((SSTV_STATUS_H - FONT_SIZE_1_HEIGHT) / 2), st->modeName, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	if (st->state == SSTV_RX_RECEIVING)
	{
		snprintf(line, sizeof(line), "%u/%u", (unsigned)(st->row + 1), (unsigned)SSTV_RX_ROWS);
	}
	else
	{
		snprintf(line, sizeof(line), "0 NEW");
	}
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(line) * 6)), ((SSTV_STATUS_H - FONT_SIZE_1_HEIGHT) / 2), line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
}

static void updateScreen(void)
{
	sstvRxStatus_t st;
	aprsRxStatus_t rx;

	sstvRxGetStatus(&st);
	aprsRxGetStatus(&rx);

	// a new picture starts: the whole screen goes to the picture
	if ((st.state == SSTV_RX_RECEIVING) && (lastState != SSTV_RX_RECEIVING))
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayFillRect(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, true);
		displayThemeResetToDefault();
		hasPicture = true;
	}
#if defined(HAS_EVENT_LOG)
	if ((st.state == SSTV_RX_DONE) && (lastState == SSTV_RX_RECEIVING))
	{
		eventLogAdd(EVENT_LOG_SSTV, "%s %u lines", st.modeName, (unsigned)(st.row + 1));
	}
#endif
	lastState = st.state;

	if (hasPicture)
	{
		drawPictureBar(&st, &rx);
	}
	else
	{
		drawIdle(&st, &rx);
	}
	displayRender();
}

static void sstvScreenStart(void)
{
	hasPicture = false;
	lastState = SSTV_RX_IDLE;
	lastDraw = 0;
	aprsRxSetMonitor(true);
	aprsRxSetSstv(true, sstvPixel);
	updateScreen();
}

static void sstvScreenStop(void)
{
	aprsRxSetSstv(false, NULL);
	aprsRxSetMonitor(false);
}

menuStatus_t menuSSTV(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		sstvScreenStart();
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
			if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				sstvScreenStop();
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				sstvRxRestart();
				hasPicture = false;
				lastState = SSTV_RX_IDLE;
				soundSetMelody(MELODY_ACK_BEEP);
				updateScreen();
			}
		}
	}

	if ((ev->time - lastDraw) >= SSTV_REDRAW_MS)
	{
		lastDraw = ev->time;
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuSSTV(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
