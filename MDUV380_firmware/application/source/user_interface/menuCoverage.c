/*
 * Fork: coverage survey (main menu > Coverage), SP7TEAM firmware by Marcin SP7TQ.
 *
 * Walk or drive around with the radio on a repeater / net channel: while recording, the
 * signal level of the current channel is stored with the GPS position every 1..30 s
 * (SPI flash, 3840 points). tools/opengd77_coverage.py turns them into a coloured KML
 * map, GPX or CSV: where the repeater is heard, and where the gaps are.
 *
 * GREEN starts / stops recording, '#' changes the interval, '0' then GREEN clears the
 * memory, RED leaves (and stops recording). Points are only stored with a GPS fix.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
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
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(HAS_FIELD_TOOLS)

#define HIST_LEN       78           // 2 px per sample, 156 px wide
#define GRAPH_TOP_DBM  (-50)
#define GRAPH_BOT_DBM  (-130)
#define FOOTER_H       11

static const uint8_t intervals[] = { 1, 2, 5, 10, 30 };

typedef struct
{
	bool     recording;
	bool     newSession;
	bool     confirmClear;
	bool     full;
	uint8_t  intervalIdx;
	uint8_t  histHead;
	uint8_t  histCount;
	int8_t   hist[HIST_LEN];        // dBm + 128 (clamped), newest at histHead - 1
	int16_t  sessMin;
	int16_t  sessMax;
	uint32_t sessCount;
	uint32_t lastSample;
	uint32_t lastHist;
} covState_t;

_Static_assert(sizeof(covState_t) <= FIELD_SCRATCH_SIZE, "coverage state too big");
#define cov (*(covState_t *)fieldToolsScratch) // CCM scratch, set up on entry

static bool hasFix(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) &&
			((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return true;
	}
#else
	UNUSED_PARAMETER(lat);
	UNUSED_PARAMETER(lon);
#endif
	return false;
}

static bool carrier(void)
{
	return (LedRead(LED_GREEN) || ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) != 0));
}

static void sUnits(int dBm, char *buf, size_t size)
{
	if (dBm > -93)
	{
		snprintf(buf, size, "S9+%d", (dBm + 93));
	}
	else
	{
		int s = (9 + ((dBm + 93) / 6));
		snprintf(buf, size, "S%d", ((s < 0) ? 0 : s));
	}
}

static void text(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg)
{
	displayThemeApply(fg, THEME_ITEM_BG);
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static int16_t graphY(int dBm, int16_t top, int16_t h)
{
	if (dBm > GRAPH_TOP_DBM)
	{
		dBm = GRAPH_TOP_DBM;
	}
	if (dBm < GRAPH_BOT_DBM)
	{
		dBm = GRAPH_BOT_DBM;
	}
	return (int16_t)(top + (((GRAPH_TOP_DBM - dBm) * (h - 1)) / (GRAPH_TOP_DBM - GRAPH_BOT_DBM)));
}

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	double lat, lon;
	bool fix = hasFix(&lat, &lon);
	themeItem_t level = ((dBm >= -100) ? THEME_ITEM_FG_RSSI_BAR : ((dBm >= -115) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER));

	displayClearBuf();
	menuDisplayTitle("COVERAGE");

	// state chip
	{
		const char *st = (cov.full ? "FULL" : (cov.recording ? (fix ? "REC" : "NO FIX") : "STOP"));
		themeItem_t c = (cov.full ? THEME_ITEM_FG_TX_COUNTER : (cov.recording ? (fix ? THEME_ITEM_FG_TX_COUNTER : THEME_ITEM_FG_RSSI_BAR_S9P) : THEME_ITEM_FG_DECORATION));
		int16_t w = (int16_t)((strlen(st) * 6) + 4);

		{
			displayThemeApply(c, THEME_ITEM_BG_MENU_NAME);
			displayFillRect((DISPLAY_SIZE_X - 2 - w), 2, w, 10, false);
			displayPrintCore((DISPLAY_SIZE_X - w), 3, st, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
		}
	}

	// level
	int16_t y = (MENU_HEADER_HEIGHT + 2);
	snprintf(buf, sizeof(buf), "%d", dBm);
	text(2, y, buf, FONT_SIZE_4, level);
	sUnits(dBm, buf, sizeof(buf));
	text(84, (y + 2), buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT);
	text(84, (y + 20), (carrier() ? "dBm  SIGNAL" : "dBm  noise"), FONT_SIZE_1, (carrier() ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION));

	// graph of the last samples (1 per second)
	int16_t gTop = (y + 34);
	int16_t gH = 30;
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawRect(0, (gTop - 1), DISPLAY_SIZE_X, (gH + 2), true);
	for (int ref = -60; ref >= -120; ref -= 20) // grid every 20 dB
	{
		int16_t gy = graphY(ref, gTop, gH);
		for (int16_t x = 2; x < (DISPLAY_SIZE_X - 2); x += 4)
		{
			displaySetPixel(x, gy, true);
		}
	}
	for (int i = 0; i < cov.histCount; i++)
	{
		int idx = ((cov.histHead + HIST_LEN - cov.histCount + i) % HIST_LEN);
		int v = (cov.hist[idx] - 128);
		int16_t x = (int16_t)(DISPLAY_SIZE_X - 2 - ((cov.histCount - i) * 2));
		int16_t gy = graphY(v, gTop, gH);

		displayThemeApply(((v >= -100) ? THEME_ITEM_FG_RSSI_BAR : ((v >= -115) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER)), THEME_ITEM_BG);
		displayFillRect(x, gy, 2, ((gTop + gH) - gy), false);
	}

	// counters
	y = (gTop + gH + 3);
	snprintf(buf, sizeof(buf), "PTS %u/%u  EVERY %us", (unsigned)fieldCoverageCount(), (unsigned)COVERAGE_POINTS_MAX, (unsigned)intervals[cov.intervalIdx]);
	text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT);

	y += 10;
	if (cov.confirmClear)
	{
		text(2, y, "CLEAR ALL? GREEN = yes", FONT_SIZE_1_BOLD, THEME_ITEM_FG_TX_COUNTER);
	}
	else if (cov.sessCount > 0U)
	{
		snprintf(buf, sizeof(buf), "SES %u  MIN %d  MAX %d", (unsigned)cov.sessCount, cov.sessMin, cov.sessMax);
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_RX_FREQ);
	}
	else if (fix)
	{
		char loc[12] = { 0 };

		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		snprintf(buf, sizeof(buf), "GPS fix  %s", loc);
		text(2, y, buf, FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR);
	}
	else
	{
		text(2, y, "No GPS fix - not logging", FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P);
	}

	// key bar
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 2), (cov.recording ? "GRN STOP  # INT  RED EXIT" : "GRN REC  # INT  0 CLR"), FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	displayThemeResetToDefault();
	displayRender();
}

static void sample(void)
{
	coveragePoint_t p;
	double lat, lon;
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

	if (hasFix(&lat, &lon) == false)
	{
		return;
	}

	p.time = ((uiDataGlobal.dateTimeSecs > 86400U) ? uiDataGlobal.dateTimeSecs : 0U);
	p.lat = (int32_t)lround(lat * 1e6);
	p.lon = (int32_t)lround(lon * 1e6);
	p.dBm = (int16_t)dBm;
	p.flags = (COVERAGE_FLAG_FIX | (carrier() ? COVERAGE_FLAG_CARRIER : 0U) |
			((trxGetMode() == RADIO_MODE_DIGITAL) ? COVERAGE_FLAG_DMR : 0U) | (cov.newSession ? COVERAGE_FLAG_SESSION : 0U));
	p.freqKHz10 = (uint8_t)((currentChannelData->rxFreq / 1000U) & 0xFFU);

	if (fieldCoverageAdd(&p) == false)
	{
		cov.recording = false;
		cov.full = true;
		soundSetMelody(MELODY_ERROR_BEEP);
		return;
	}

	cov.newSession = false;
	if ((cov.sessCount == 0U) || (dBm < cov.sessMin))
	{
		cov.sessMin = (int16_t)dBm;
	}
	if ((cov.sessCount == 0U) || (dBm > cov.sessMax))
	{
		cov.sessMax = (int16_t)dBm;
	}
	cov.sessCount++;
}

menuStatus_t menuCoverage(uiEvent_t *ev, bool isFirstRun)
{
	uint32_t now = ticksGetMillis();

	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		memset(&cov, 0, sizeof(covState_t));
		cov.intervalIdx = 2; // 5 s
		cov.confirmClear = false;
		cov.full = (fieldCoverageCount() >= COVERAGE_POINTS_MAX);
		cov.histCount = 0;
		cov.histHead = 0;
		cov.lastHist = now;
		updateScreen();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent && (ev->events & KEY_EVENT))
	{
		if (cov.confirmClear)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				fieldCoverageClear();
				cov.full = false;
				cov.sessCount = 0U;
				soundSetMelody(MELODY_ACK_BEEP);
			}
			cov.confirmClear = false;
			updateScreen();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			cov.recording = false;
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (cov.full)
			{
				soundSetMelody(MELODY_ERROR_BEEP);
			}
			else
			{
				cov.recording = !cov.recording;
				if (cov.recording)
				{
					cov.newSession = true;
					cov.sessCount = 0U;
					cov.lastSample = (now - (intervals[cov.intervalIdx] * 1000U)); // first point now
				}
				soundSetMelody(MELODY_ACK_BEEP);
			}
			updateScreen();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			cov.intervalIdx = (uint8_t)((cov.intervalIdx + 1U) % sizeof(intervals));
			updateScreen();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_0) && (cov.recording == false))
		{
			cov.confirmClear = true;
			updateScreen();
		}
	}
	else if (ev->hasEvent && (ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen();
	}

	if (cov.recording && (trxTransmissionEnabled == false) && ((now - cov.lastSample) >= (intervals[cov.intervalIdx] * 1000U)))
	{
		cov.lastSample = now;
		sample();
	}

	if ((now - cov.lastHist) >= 1000U)
	{
		int v = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY) + 128;

		cov.lastHist = now;
		cov.hist[cov.histHead] = (int8_t)((v < 0) ? 0 : ((v > 127) ? 127 : v));
		cov.histHead = (uint8_t)((cov.histHead + 1U) % HIST_LEN);
		if (cov.histCount < HIST_LEN)
		{
			cov.histCount++;
		}
		updateScreen();
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuCoverage(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
