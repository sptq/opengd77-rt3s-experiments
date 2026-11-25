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
#include "user_interface/uiEmcom.h"

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
	bool       confirmClear; // '0' pressed: GREEN clears the reports
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

static const char *const numberWords[] = { "", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE" };

static void reportTime(const rcReport_t *r, char *buf, size_t size)
{
	snprintf(buf, size, "--:--");
	if (r->time != 0U)
	{
		time_t_custom secs = r->time;
		struct tm tmv;

		gmtime_r_Custom(&secs, &tmv);
		snprintf(buf, size, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
	}
}

#if defined(HAS_EMCOM_UI)
// EMCOM style: live level on the status strip, the last report big with its rating (OK / WEAK /
// POOR, a word as well as a colour), the older reports as rows, the key bar
static const char *ratingWord(int rd, themeItem_t *colour)
{
	*colour = ((rd >= 4) ? THEME_ITEM_FG_RSSI_BAR : ((rd == 3) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER));
	return ((rd >= 4) ? "OK" : ((rd == 3) ? "WEAK" : "POOR"));
}

#if defined(EMCOM_UI_MONO)
// 128 x 64 mono: title, live level row, the last report ("5 BY 9" 8x16 with its rating, length,
// mode, average / peak dBm), the two older reports as 9 px rows, the key bar or the confirmation
#define MONO_LINE_CHARS  21

static void updateScreenMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	size_t lineSize = SAFE_MIN(sizeof(buf), (size_t)(MONO_LINE_CHARS + 1));
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	bool rx = carrier();
	themeItem_t colour;
	const char *word;
	int16_t x;
	int16_t rightX;

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)rc.count, (unsigned)REPORTS);
	emcomUiTitle("RADIO CHECK", buf);

	// live level row (y 10): carrier, S level, dBm ; length of the over being received, or the mode
	if (rc.active)
	{
		uint32_t ds = ((ticksGetMillis() - rc.start) / 100U);

		snprintf(buf, lineSize, "%u.%us", (unsigned)(ds / 10U), (unsigned)(ds % 10U));
	}
	else
	{
		snprintf(buf, lineSize, "%s", ((trxGetMode() == RADIO_MODE_DIGITAL) ? "DMR" : "FM"));
	}
	rightX = (int16_t)(DISPLAY_SIZE_X - 1 - (strlen(buf) * 6));
	emcomUiText((DISPLAY_SIZE_X - 1), 10, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, true);

	x = (int16_t)(emcomUiChip(0, 10, (rx ? "RX" : "IDLE"), THEME_ITEM_FG_DEFAULT, (rx ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 3);
	sText(dBm, buf, lineSize);
	x = (int16_t)(x + emcomUiText(x, 10, buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false) + 4);
	snprintf(buf, lineSize, "%ddBm", dBm);
	if ((x + (int16_t)(strlen(buf) * 6)) > (rightX - 3))
	{
		buf[strlen(buf) - 3] = 0; // no room for the unit
	}
	if ((x + (int16_t)(strlen(buf) * 6)) <= (rightX - 3))
	{
		emcomUiText(x, 10, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}

	if (rc.count == 0U)
	{
		displayPrintCentered(20, "LISTENING", FONT_SIZE_3);
		emcomUiText(0, 37, "Each received over is", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(0, 46, "rated R1-5 / S1-9.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
	}
	else
	{
		const rcReport_t *r = &rc.reports[0];
		int rd = readability(r->avg, r->dmr);
		int st = strength(r->avg);

		// last report (y 19..35): "5 BY 9" ; rating chip + length, mode + average / peak dBm
		snprintf(buf, lineSize, "%d BY %d", rd, st);
		emcomUiText(0, 19, buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
		word = ratingWord(rd, &colour);
		x = (int16_t)(52 + emcomUiChip(52, 20, word, colour, EMCOM_CHIP_SOLID, false) + 3);
		snprintf(buf, lineSize, "%u.%us", (unsigned)(r->lenDs / 10U), (unsigned)(r->lenDs % 10U));
		if ((x + (int16_t)(strlen(buf) * 6)) <= DISPLAY_SIZE_X)
		{
			emcomUiText(x, 20, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
		}
		snprintf(buf, lineSize, "%s %d/%d", (r->dmr ? "DMR" : "FM"), r->avg, r->peak);
		if ((52 + (int16_t)(strlen(buf) * 6)) > DISPLAY_SIZE_X)
		{
			snprintf(buf, lineSize, "%d/%d", r->avg, r->peak); // no room for the mode
		}
		emcomUiText(52, 28, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);

		// older reports (y 37, 46): time, "4 BY 9", average dBm, rating chip
		int16_t y = 37;

		for (int i = 1; (i < rc.count) && (y <= 46); i++, y += 9)
		{
			const rcReport_t *o = &rc.reports[i];
			int ord = readability(o->avg, o->dmr);

			reportTime(o, buf, lineSize);
			emcomUiText(0, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			snprintf(buf, lineSize, "%d BY %d", ord, strength(o->avg));
			emcomUiText(33, y, buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
			snprintf(buf, lineSize, "%d", o->avg);
			emcomUiText(72, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			word = ratingWord(ord, &colour);
			emcomUiChip((DISPLAY_SIZE_X - 1), y, word, colour, EMCOM_CHIP_SOLID, true);
		}
	}

	if (rc.confirmClear)
	{
		emcomUiConfirmBar("CLEAR?");
	}
	else
	{
		emcomUiKeyBar((rc.count > 0U) ? "0 CLEAR  RED EXIT" : "RED EXIT");
	}
	displayRender();
}
#else
static void updateScreenEmcom(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	bool rx = carrier();
	int16_t y = EMCOM_UI_BODY_Y;
	themeItem_t colour;
	const char *word;

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)rc.count, (unsigned)REPORTS);
	emcomUiTitle("RADIO CHECK", buf);

	// status strip: carrier, live level, length of the over being received
	emcomUiStrip();
	int16_t x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), (rx ? "RX" : "IDLE"), (rx ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION),
			(rx ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 4);
	sText(dBm, buf, sizeof(buf));
	x += (int16_t)(emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_HEADER_TEXT, false) + 6);
	snprintf(buf, sizeof(buf), "%ddBm", dBm);
	emcomUiText(x, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
	if (rc.active)
	{
		uint32_t ds = ((ticksGetMillis() - rc.start) / 100U);

		snprintf(buf, sizeof(buf), "%u.%us", (unsigned)(ds / 10U), (unsigned)(ds % 10U));
		emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR, true);
	}
	else
	{
		emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), ((trxGetMode() == RADIO_MODE_DIGITAL) ? "DMR" : "FM"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
	}

	if (rc.count == 0U)
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((y + 14), "LISTENING", FONT_SIZE_3);
		displayThemeResetToDefault();
		emcomUiText(4, (y + 40), "Each received over is", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
		emcomUiText(4, (y + 50), "rated R1-5 / S1-9.", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
	}
	else
	{
		const rcReport_t *r = &rc.reports[0];
		int rd = readability(r->avg, r->dmr);
		int st = strength(r->avg);

		// last report: "5 BY 9" + rating
		word = ratingWord(rd, &colour);
		snprintf(buf, sizeof(buf), "%d BY %d", rd, st);
		emcomUiText(2, y, buf, FONT_SIZE_4, colour, false);
		int16_t cw = (int16_t)((strlen(word) * 8) + 6);
		displayThemeApply(colour, THEME_ITEM_BG);
		displayFillRect((DISPLAY_SIZE_X - 2 - cw), (y + 7), cw, 18, false);
		displayPrintCore((DISPLAY_SIZE_X - 2 - cw + 3), (y + 8), word, FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
		displayThemeResetToDefault();

		snprintf(buf, sizeof(buf), "%s BY %s", numberWords[rd], numberWords[st]);
		emcomUiText(2, (y + 34), buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
		snprintf(buf, sizeof(buf), "%s %u.%us AVG %d PK %d", (r->dmr ? "DMR" : "FM"), (unsigned)(r->lenDs / 10U), (unsigned)(r->lenDs % 10U), r->avg, r->peak);
		emcomUiText(2, (y + 44), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);

		// older reports
		y += 54;
		emcomUiDivider(y);
		y += 3;
		for (int i = 1; (i < rc.count) && (y <= (EMCOM_UI_KEYBAR_Y - 10)); i++, y += 11)
		{
			const rcReport_t *o = &rc.reports[i];
			int ord = readability(o->avg, o->dmr);

			reportTime(o, buf, sizeof(buf));
			emcomUiText(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			snprintf(buf, sizeof(buf), "%d BY %d", ord, strength(o->avg));
			emcomUiText(36, y, buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);
			snprintf(buf, sizeof(buf), "%ddBm", o->avg);
			emcomUiText(80, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
			word = ratingWord(ord, &colour);
			emcomUiChip((DISPLAY_SIZE_X - 2), y, word, colour, EMCOM_CHIP_SOLID, true);
		}
	}

	if (rc.confirmClear)
	{
		emcomUiConfirmBar("CLEAR ALL?");
	}
	else
	{
		emcomUiKeyBar((rc.count > 0U) ? "0 CLEAR  RED EXIT" : "RED EXIT");
	}
	displayRender();
}
#endif // EMCOM_UI_MONO
#endif

static void updateScreen(void)
{
	const char *const *words = numberWords;
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	bool rx = carrier();
	int16_t y = (MENU_HEADER_HEIGHT + 2);

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
			char t[8];

			reportTime(o, t, sizeof(t));
			snprintf(buf, sizeof(buf), "%s  %d BY %d  %d dBm", t, readability(o->avg, o->dmr), strength(o->avg), o->avg);
			text(2, ry, buf, FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ);
		}
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	if (rc.confirmClear)
	{
		displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 2), "CLEAR ALL?", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(69, (fy + 2), "GREEN = yes", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore(3, (fy + 2), "0 CLEAR  RED EXIT", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

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
			if (rc.confirmClear)
			{
				// decided on the key release (the press of GREEN would cancel); RED cancels, stays here
				if (ev->keys.event & KEY_MOD_UP)
				{
					if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
					{
						rc.count = 0U;
						soundSetMelody(MELODY_ACK_BEEP);
					}
					rc.confirmClear = false;
					updateScreen();
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0) && (rc.count > 0U))
			{
				rc.confirmClear = true;
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
