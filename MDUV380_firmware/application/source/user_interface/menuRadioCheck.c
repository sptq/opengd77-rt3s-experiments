/*
 * Fork: radio check (main menu > Radio check), SP7TEAM firmware by Marcin SP7TQ.
 *
 * "Radio check, how do you read?": every received transmission is measured (duration,
 * average and peak level) and turned into a readability / strength report, ready to be
 * read back ("five by nine"). The live level is shown on top, the last reports below.
 * Readability is estimated from the level (FM: noise grows below about -110 dBm; DMR:
 * the voice is clean until it drops out).
 * '0' clears the reports, RED / GREEN leave.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/trx.h"
#include "functions/fieldData.h"
#include "io/LEDs.h"

#if defined(HAS_FIELD_TOOLS)

#define REPORTS        3
#define END_HOLD_MS    400U   // carrier gone for this long = end of the transmission
#define MIN_LEN_MS     300U   // shorter bursts are ignored
#define FOOTER_H       11

typedef struct
{
	uint32_t time;     // UTC seconds (0: no clock)
	int16_t  avg;
	int16_t  peak;
	uint16_t lenDs;    // tenths of a second
	uint8_t  dmr;
} rcReport_t;

typedef struct
{
	bool       active;
	uint8_t    count;
	uint32_t   start;
	uint32_t   lastSeen;
	uint32_t   lastSample;
	int32_t    sum;
	uint16_t   n;
	int16_t    peak;
	rcReport_t reports[REPORTS]; // [0] = newest
} rcState_t;

_Static_assert(sizeof(rcState_t) <= FIELD_SCRATCH_SIZE, "radio check state too big");
#define rc (*(rcState_t *)fieldToolsScratch) // CCM scratch, set up on entry

static bool carrier(void)
{
	return (LedRead(LED_GREEN) || ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) != 0));
}

static int readability(int dBm, bool dmr)
{
	if (dmr)
	{
		return ((dBm >= -110) ? 5 : ((dBm >= -117) ? 4 : 3));
	}
	return ((dBm >= -105) ? 5 : ((dBm >= -110) ? 4 : ((dBm >= -115) ? 3 : ((dBm >= -120) ? 2 : 1))));
}

static int strength(int dBm)
{
	int s = (9 + ((dBm + 93) / 6));
	return ((s < 1) ? 1 : ((s > 9) ? 9 : s));
}

static void sText(int dBm, char *buf, size_t size)
{
	if (dBm > -93)
	{
		snprintf(buf, size, "S9+%d", (dBm + 93));
	}
	else
	{
		snprintf(buf, size, "S%d", strength(dBm));
	}
}

static void text(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg)
{
	displayThemeApply(fg, THEME_ITEM_BG);
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static void updateScreen(void)
{
	static const char *const words[] = { "", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE" };
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	bool rx = carrier();
	int16_t y = (MENU_HEADER_HEIGHT + 2);

	displayClearBuf();
	menuDisplayTitle("RADIO CHECK");

	// live level
	displayThemeApply((rx ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG_MENU_NAME);
	displayFillRect((DISPLAY_SIZE_X - 22), 2, 20, 10, false);
	displayPrintCore((DISPLAY_SIZE_X - 19), 3, (rx ? "RX" : "--"), FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);

	sText(dBm, buf, sizeof(buf));
	text(2, y, buf, FONT_SIZE_3, (rx ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION));
	snprintf(buf, sizeof(buf), "%d dBm", dBm);
	text(60, (y + 4), buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);
	if (rc.active)
	{
		snprintf(buf, sizeof(buf), "%u.%us", (unsigned)((ticksGetMillis() - rc.start) / 1000U), (unsigned)(((ticksGetMillis() - rc.start) / 100U) % 10U));
		text(120, (y + 4), buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR);
	}

	// last report, big
	y += 20;
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, y, DISPLAY_SIZE_X, true);
	y += 3;

	if (rc.count == 0U)
	{
		text(4, (y + 8), "Waiting for a call...", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);
		text(4, (y + 20), "Each received over is", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
		text(4, (y + 30), "rated R1-5 / S1-9.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION);
	}
	else
	{
		const rcReport_t *r = &rc.reports[0];
		int rd = readability(r->avg, r->dmr);
		int st = strength(r->avg);

		snprintf(buf, sizeof(buf), "%d BY %d", rd, st);
		text(2, y, buf, FONT_SIZE_4, ((rd >= 4) ? THEME_ITEM_FG_RSSI_BAR : ((rd == 3) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER)));

		snprintf(buf, sizeof(buf), "%s BY %s", words[rd], words[st]);
		text(2, (y + 33), buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT);
		snprintf(buf, sizeof(buf), "%s %u.%us AVG %d PK %d", (r->dmr ? "DMR" : "FM"), (unsigned)(r->lenDs / 10U), (unsigned)(r->lenDs % 10U), r->avg, r->peak);
		text(2, (y + 43), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION);

		// older reports
		int16_t ry = (y + 55);
		for (int i = 1; (i < rc.count) && (ry <= (DISPLAY_SIZE_Y - FOOTER_H - 9)); i++, ry += 10)
		{
			const rcReport_t *o = &rc.reports[i];
			char t[8] = "--:--";

			if (o->time != 0U)
			{
				time_t_custom secs = o->time;
				struct tm tmv;

				gmtime_r_Custom(&secs, &tmv);
				snprintf(t, sizeof(t), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
			}
			snprintf(buf, sizeof(buf), "%s  %d BY %d  %d dBm", t, readability(o->avg, o->dmr), strength(o->avg), o->avg);
			text(2, ry, buf, FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ);
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 2), "0 CLEAR  RED EXIT", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeResetToDefault();
	displayRender();
}

static void finishOver(uint32_t now)
{
	uint32_t len = (rc.lastSeen - rc.start);

	rc.active = false;
	if ((len < MIN_LEN_MS) || (rc.n == 0U))
	{
		return;
	}

	memmove(&rc.reports[1], &rc.reports[0], (sizeof(rcReport_t) * (REPORTS - 1)));
	rc.reports[0].time = ((uiDataGlobal.dateTimeSecs > 86400U) ? uiDataGlobal.dateTimeSecs : 0U);
	rc.reports[0].avg = (int16_t)(rc.sum / rc.n);
	rc.reports[0].peak = rc.peak;
	rc.reports[0].lenDs = (uint16_t)SAFE_MIN((len / 100U), 65535U);
	rc.reports[0].dmr = (trxGetMode() == RADIO_MODE_DIGITAL);
	if (rc.count < REPORTS)
	{
		rc.count++;
	}
	UNUSED_PARAMETER(now);
	soundSetMelody(MELODY_ACK_BEEP);
}

menuStatus_t menuRadioCheck(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t lastDraw = 0U;
	uint32_t now = ticksGetMillis();

	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		memset(&rc, 0, sizeof(rcState_t));
		lastDraw = now;
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
			if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				rc.count = 0U;
				updateScreen();
			}
		}
	}

	// measure the transmissions (every 100 ms)
	if ((now - rc.lastSample) >= 100U)
	{
		rc.lastSample = now;

		if ((trxTransmissionEnabled == false) && carrier())
		{
			int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

			if (rc.active == false)
			{
				rc.active = true;
				rc.start = now;
				rc.sum = 0;
				rc.n = 0U;
				rc.peak = (int16_t)dBm;
			}
			rc.lastSeen = now;
			if ((now - rc.start) >= 200U) // skip the squelch opening
			{
				rc.sum += dBm;
				rc.n++;
				if (dBm > rc.peak)
				{
					rc.peak = (int16_t)dBm;
				}
			}
		}
		else if (rc.active && ((now - rc.lastSeen) >= END_HOLD_MS))
		{
			finishOver(now);
			updateScreen();
			lastDraw = now;
		}
	}

	if ((now - lastDraw) >= 500U)
	{
		lastDraw = now;
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuRadioCheck(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
