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
#include "user_interface/uiEmcom.h"
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

#if defined(HAS_EMCOM_UI)
#if !defined(EMCOM_UI_MONO)
static themeItem_t levelColour(int dBm)
{
	return ((dBm >= -100) ? THEME_ITEM_FG_RSSI_BAR : ((dBm >= -115) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER));
}

// EMCOM style: recording state / interval / memory on the status strip, the level big with a
// word (GOOD / WEAK / GAP), the level graph, the session and GPS rows, the key bar. With the
// shape cues (mono / contrast) weak samples are striped and gaps only a tick on the graph.
#define EM_GRAPH_H   26

static void updateScreenEmcom(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	double lat, lon;
	bool fix = hasFix(&lat, &lon);
	bool cues = emcomShapeCues();
	uint32_t points = fieldCoverageCount();
	int16_t y = EMCOM_UI_BODY_Y;

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)points, (unsigned)COVERAGE_POINTS_MAX);
	emcomUiTitle("COVERAGE", buf);

	// status strip: state, interval, memory used
	emcomUiStrip();
	{
		const char *st = (cov.full ? "FULL" : (cov.recording ? (fix ? "REC" : "NO FIX") : "STOP"));
		themeItem_t c = (cov.full ? THEME_ITEM_FG_TX_COUNTER : (cov.recording ? (fix ? THEME_ITEM_FG_TX_COUNTER : THEME_ITEM_FG_RSSI_BAR_S9P) : THEME_ITEM_FG_DECORATION));
		int16_t x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), st, c, ((cov.recording || cov.full) ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 4);

		snprintf(buf, sizeof(buf), "%us", (unsigned)intervals[cov.intervalIdx]);
		emcomUiKeyValue(x, (EMCOM_UI_STRIP_Y + 2), "EVERY", buf, THEME_ITEM_FG_HEADER_TEXT);
		snprintf(buf, sizeof(buf), "%u%%", (unsigned)((points * 100U) / COVERAGE_POINTS_MAX));
		int16_t w = emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, true);
		emcomUiText((DISPLAY_SIZE_X - 3 - w - 4), (EMCOM_UI_STRIP_Y + 2), "MEM", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
	}

	// level: dBm big, S units, carrier, a word for the level
	snprintf(buf, sizeof(buf), "%d", dBm);
	emcomUiText(2, y, buf, FONT_SIZE_4, levelColour(dBm), false);
	sUnits(dBm, buf, sizeof(buf));
	emcomUiText(84, (y + 2), buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
	emcomUiChip((DISPLAY_SIZE_X - 2), (y + 5), (carrier() ? "RF" : "--"), (carrier() ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION),
			(carrier() ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), true);
	int16_t x = (int16_t)(84 + emcomUiText(84, (y + 21), "dBm", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false) + 6);
	emcomUiText(x, (y + 21), ((dBm >= -100) ? "GOOD" : ((dBm >= -115) ? "WEAK" : "GAP")), FONT_SIZE_1_BOLD, levelColour(dBm), false);

	// graph of the last samples (1 per second)
	int16_t gTop = (y + 34);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawRect(0, (gTop - 1), DISPLAY_SIZE_X, (EM_GRAPH_H + 2), true);
	for (int ref = -60; ref >= -120; ref -= 20) // grid every 20 dB
	{
		int16_t gy = graphY(ref, gTop, EM_GRAPH_H);
		for (int16_t gx = 2; gx < (DISPLAY_SIZE_X - 2); gx += 4)
		{
			displaySetPixel(gx, gy, true);
		}
	}
	for (int i = 0; i < cov.histCount; i++)
	{
		int idx = ((cov.histHead + HIST_LEN - cov.histCount + i) % HIST_LEN);
		int v = (cov.hist[idx] - 128);
		int16_t gx = (int16_t)(DISPLAY_SIZE_X - 2 - ((cov.histCount - i) * 2));
		int16_t gy = graphY(v, gTop, EM_GRAPH_H);
		int16_t h = ((gTop + EM_GRAPH_H) - gy);

		displayThemeApply(levelColour(v), THEME_ITEM_BG);
		if (cues && (v < -115))
		{
			displayFillRect(gx, gy, 2, SAFE_MIN(h, 2), false); // gap: a tick only
		}
		else if (cues && (v < -100))
		{
			for (int16_t r = 0; r < h; r += 2) // weak: striped
			{
				displayFillRect(gx, (gy + r), 2, 1, false);
			}
		}
		else
		{
			displayFillRect(gx, gy, 2, h, false);
		}
	}
	displayThemeResetToDefault();

	// session, GPS
	y = (gTop + EM_GRAPH_H + 4);
	if (cov.sessCount > 0U)
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)cov.sessCount);
		x = (int16_t)(2 + emcomUiKeyValue(2, y, "SES", buf, THEME_ITEM_FG_DEFAULT) + 8);
		snprintf(buf, sizeof(buf), "%d", cov.sessMin);
		x = (int16_t)(x + emcomUiKeyValue(x, y, "MIN", buf, levelColour(cov.sessMin)) + 8);
		snprintf(buf, sizeof(buf), "%d", cov.sessMax);
		emcomUiKeyValue(x, y, "MAX", buf, levelColour(cov.sessMax));
	}
	else
	{
		emcomUiText(2, y, (cov.recording ? "Session: no points yet" : "GRN starts a session"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false);
	}

	y += 10;
	if (fix)
	{
		char loc[12] = { 0 };

		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		x = (int16_t)(2 + emcomUiChip(2, y, "GPS", THEME_ITEM_FG_RSSI_BAR, EMCOM_CHIP_SOLID, false) + 4);
		emcomUiText(x, y, loc, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}
	else
	{
		x = (int16_t)(2 + emcomUiChip(2, y, "NO GPS", THEME_ITEM_FG_RSSI_BAR_S9P, EMCOM_CHIP_SOLID, false) + 4);
		emcomUiText(x, y, "not logging", FONT_SIZE_1, THEME_ITEM_FG_RSSI_BAR_S9P, false);
	}

	if (cov.confirmClear)
	{
		emcomUiConfirmBar("CLEAR ALL?");
	}
	else
	{
		emcomUiKeyBar(cov.recording ? "GRN STOP  # INT  RED EXIT" : "GRN REC  # INT  0 CLR");
	}
	displayRender();
}
#endif // !EMCOM_UI_MONO

#if defined(EMCOM_UI_MONO)
// 128 x 64: title with the stored points, the state / interval / memory row, the level 8x16 with
// S units and a word beside the graph of the last 62 s, the session row with the carrier chip,
// the GPS row and the key bar. Weak samples are striped, gaps only a tick (no colour).
#define MONO_GRAPH_X   64
#define MONO_GRAPH_Y   19
#define MONO_GRAPH_W   64
#define MONO_GRAPH_H   17

static void updateScreenMono(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	double lat, lon;
	bool fix = hasFix(&lat, &lon);
	bool rf = carrier();
	uint32_t points = fieldCoverageCount();
	int16_t x, w;

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)points, (unsigned)COVERAGE_POINTS_MAX);
	emcomUiTitle("COVERAGE", buf);

	// state, interval, memory used (9..17)
	{
		const char *st = (cov.full ? "FULL" : (cov.recording ? (fix ? "REC" : "NO FIX") : "STOP"));

		x = (int16_t)(emcomUiChip(0, 10, st, THEME_ITEM_FG_DEFAULT, ((cov.recording || cov.full) ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), false) + 3);
		snprintf(buf, sizeof(buf), "%us", (unsigned)intervals[cov.intervalIdx]);
		emcomUiKeyValue(x, 10, "EV", buf, THEME_ITEM_FG_DEFAULT);
		snprintf(buf, sizeof(buf), "%u%%", (unsigned)((points * 100U) / COVERAGE_POINTS_MAX));
		w = emcomUiText((DISPLAY_SIZE_X - 1), 10, buf, FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, true);
		emcomUiText((DISPLAY_SIZE_X - 1 - w - 3), 10, "MEM", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, true);
	}

	// level: dBm 8x16, S units and GOOD / WEAK / GAP beside it (19..34)
	snprintf(buf, sizeof(buf), "%d", dBm);
	w = emcomUiText(0, 19, buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, false);
	sUnits(dBm, buf, sizeof(buf));
	emcomUiText((w + 2), 19, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	emcomUiText((w + 2), 27, ((dBm >= -100) ? "GOOD" : ((dBm >= -115) ? "WEAK" : "GAP")), FONT_SIZE_1_BOLD, THEME_ITEM_FG_DEFAULT, false);

	// graph of the last samples (1 per second, 1 px each), right half (64..127, 19..35)
	{
		int16_t gTop = (MONO_GRAPH_Y + 1);
		int16_t gH = (MONO_GRAPH_H - 2);
		int16_t gRight = (MONO_GRAPH_X + MONO_GRAPH_W - 2);
		int n = SAFE_MIN((int)cov.histCount, (MONO_GRAPH_W - 2));

		displayDrawRect(MONO_GRAPH_X, MONO_GRAPH_Y, MONO_GRAPH_W, MONO_GRAPH_H, true);
		for (int ref = -60; ref >= -120; ref -= 20) // grid every 20 dB
		{
			int16_t gy = graphY(ref, gTop, gH);
			for (int16_t gx = (MONO_GRAPH_X + 2); gx < gRight; gx += 4)
			{
				displaySetPixel(gx, gy, true);
			}
		}
		for (int i = 0; i < n; i++)
		{
			int idx = ((cov.histHead + HIST_LEN - n + i) % HIST_LEN);
			int v = (cov.hist[idx] - 128);
			int16_t gx = (int16_t)(gRight - (n - 1 - i));
			int16_t gy = graphY(v, gTop, gH);
			int16_t h = ((gTop + gH) - gy);

			if (v < -115)
			{
				displayFillRect(gx, gy, 1, SAFE_MIN(h, 2), false); // gap: a tick only
			}
			else if (v < -100)
			{
				for (int16_t r = 0; r < h; r += 2) // weak: striped
				{
					displaySetPixel(gx, (gy + r), true);
				}
			}
			else
			{
				displayFillRect(gx, gy, 1, h, false);
			}
		}
	}

	// session: points, min / max dBm ; carrier chip on the right (37)
	if (cov.sessCount > 0U)
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)cov.sessCount);
		x = (int16_t)(emcomUiKeyValue(0, 37, "SES", buf, THEME_ITEM_FG_DEFAULT) + 5);
		snprintf(buf, sizeof(buf), "%d/%d", cov.sessMin, cov.sessMax);
		emcomUiText(x, 37, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}
	else
	{
		emcomUiText(0, 37, (cov.recording ? "No points yet" : "GRN: new session"), FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}
	emcomUiChip((DISPLAY_SIZE_X - 1), 37, (rf ? "RF" : "--"), THEME_ITEM_FG_DEFAULT, (rf ? EMCOM_CHIP_SOLID : EMCOM_CHIP_OUTLINE), true);

	// GPS (46)
	if (fix)
	{
		char loc[12] = { 0 };

		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		x = (int16_t)(emcomUiChip(0, 46, "GPS", THEME_ITEM_FG_DEFAULT, EMCOM_CHIP_SOLID, false) + 3);
		emcomUiText(x, 46, loc, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}
	else
	{
		x = (int16_t)(emcomUiChip(0, 46, "NO GPS", THEME_ITEM_FG_DEFAULT, EMCOM_CHIP_SOLID, false) + 3);
		emcomUiText(x, 46, "not logging", FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, false);
	}

	if (cov.confirmClear)
	{
		emcomUiConfirmBar("CLEAR?");
	}
	else
	{
		emcomUiKeyBar(cov.recording ? "GRN STOP #INT RED ESC" : "GRN REC  # INT  0 CLR");
	}
	displayRender();
}
#endif
#endif

static void updateScreen(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	double lat, lon;
	bool fix = hasFix(&lat, &lon);
	themeItem_t level = ((dBm >= -100) ? THEME_ITEM_FG_RSSI_BAR : ((dBm >= -115) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_TX_COUNTER));

#if defined(EMCOM_UI_MONO)
	// 128 x 64: neither the EMCOM nor the classic layout (160 x 128) fits, whatever the style setting
	updateScreenMono();
	return;
#elif defined(HAS_EMCOM_UI)
	if (emcomStyleEnabled())
	{
		updateScreenEmcom();
		return;
	}
#endif

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
			// decided on the key release: the press of GREEN cancelled, then its release started a recording
			if (ev->keys.event & KEY_MOD_UP)
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
			}
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
