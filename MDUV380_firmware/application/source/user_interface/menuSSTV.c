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

static void drawHint(void)
{
	displayFillRect(0, SSTV_Y0, DISPLAY_SIZE_X, SSTV_RX_ROWS, true);
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintCentered((SSTV_Y0 + 22), "Waiting for", FONT_SIZE_3);
	displayPrintCentered((SSTV_Y0 + 40), "SSTV picture", FONT_SIZE_3);
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	displayPrintCentered((SSTV_Y0 + 64), "FM, e.g. ISS 145.800", FONT_SIZE_1);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCentered((SSTV_Y0 + 76), "ROBOT MARTIN SCOTTIE PD", FONT_SIZE_1);
	displayDrawFastHLine(20, (SSTV_Y0 + 92), (DISPLAY_SIZE_X - 40), true);
	displayPrintCentered((SSTV_Y0 + 98), "0 CLEAR  RED EXIT", FONT_SIZE_1);
	displayThemeResetToDefault();
}

static void updateScreen(void)
{
	char line[SSTV_CHARS + 1];
	sstvRxStatus_t st;
	aprsRxStatus_t rx;

	sstvRxGetStatus(&st);
	aprsRxGetStatus(&rx);

	// a new picture starts: clear the hint / the previous picture
	if ((st.state == SSTV_RX_RECEIVING) && (lastState != SSTV_RX_RECEIVING))
	{
		displayFillRect(0, SSTV_Y0, DISPLAY_SIZE_X, SSTV_RX_ROWS, true);
		hasPicture = true;
	}
#if defined(HAS_EVENT_LOG)
	if ((st.state == SSTV_RX_DONE) && (lastState == SSTV_RX_RECEIVING))
	{
		eventLogAdd(EVENT_LOG_SSTV, "%s %u lines", st.modeName, (unsigned)(st.row + 1));
	}
#endif
	lastState = st.state;

	if (rx.state != APRS_RX_STATE_LISTENING)
	{
		snprintf(line, sizeof(line), "SSTV: %s", ((trxGetMode() != RADIO_MODE_ANALOG) ? "FM only" : ((rx.state == APRS_RX_STATE_NO_CLOCK) ? "no I2S clock" : "paused")));
	}
	else
	{
		switch (st.state)
		{
			case SSTV_RX_RECEIVING:
				snprintf(line, sizeof(line), "%s %u/%u", st.modeName, (unsigned)(st.row + 1), (unsigned)SSTV_RX_ROWS);
				break;
			case SSTV_RX_DONE:
				snprintf(line, sizeof(line), "%s done", st.modeName);
				break;
			default:
				if (st.lastVis)
				{
					snprintf(line, sizeof(line), "SSTV (VIS %u?)", (unsigned)st.lastVis); // last VIS code was not a known mode
				}
				else
				{
					snprintf(line, sizeof(line), "SSTV %ddBm", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
				}
				break;
		}

		if (st.freqHz && (strlen(line) < (SSTV_CHARS - 7)))
		{
			size_t l = strlen(line);
			snprintf(&line[l], (sizeof(line) - l), " %uHz", (unsigned)st.freqHz);
		}
	}

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, 0, DISPLAY_SIZE_X, SSTV_STATUS_H, false);
	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
	displayFillRect(0, 0, 3, SSTV_STATUS_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintAt(6, ((SSTV_STATUS_H - FONT_SIZE_1_HEIGHT) / 2), line, FONT_SIZE_1);
	displayThemeResetToDefault();
	displayRender();
}

static void sstvScreenStart(void)
{
	displayClearBuf();
	drawHint();
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
				drawHint();
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
