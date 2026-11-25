/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <time.h>
#include "user_interface/uiGlobals.h"
#include "utils.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/clockManager.h"
#include "interfaces/pit.h"
#include "functions/satellite.h"
#include "hardware/radioHardwareInterface.h"
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "interfaces/batteryAndPowerManagement.h"
#endif
#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif
#include "user_interface/uiEmcom.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(PLATFORM_RD5R)
#define NUM_PASSES_TO_DISPLAY_ON_LIST_SCREEN 2
#elif defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#if defined(PLATFORM_VARIANT_DM1701)
#define NUM_PASSES_TO_DISPLAY_ON_LIST_SCREEN 5
#else
#define NUM_PASSES_TO_DISPLAY_ON_LIST_SCREEN 6
#endif
#else
#define NUM_PASSES_TO_DISPLAY_ON_LIST_SCREEN 3
#endif


static const uint32_t ALARM_OFFSET_SECS = 60;

enum
{
	SATELLITE_SCREEN_ALL_PREDICTIONS_LIST,
	SATELLITE_SCREEN_SELECTED_SATELLITE,
	SATELLITE_SCREEN_SELECTED_SATELLITE_POLAR,
	SATELLITE_SCREEN_SELECTED_SATELLITE_PREDICTION,
	NUM_SATELLITE_SCREEN_ITEMS
};

static void handleEvent(uiEvent_t *ev);
static void updateScreen(uiEvent_t *ev, bool firstRun, bool announceVP);
static bool calculatePredictionsForSatelliteIndex(int satelliteIndex);
static void loadKeps(void);
static int menuSatelliteFindNextSatellite(void);
static void exitCallback(void *data);
static void selectSatellite(uint32_t selectedSatellite);
static void calculateActiveSatelliteData(bool forceFrequencyUpdate);

static bool hasSatelliteKeps = false;
static bool satelliteVisible = false;
static CodeplugChannel_t satelliteChannelData = { .rxFreq = 0, .sql = 10U };
static struct tm timeAndDate;
static int displayMode = SATELLITE_SCREEN_ALL_PREDICTIONS_LIST;
static uint32_t nextCalculationTime = 0;
static uint32_t menuSatelliteScreenNextUpdateTime;
static char azelBuffer[SCREEN_LINE_BUFFER_SIZE];
static uint32_t numSatellitesLoaded;
static predictionStateMachineData_t currentPrediction;
static int currentlyDisplayedListPosition = 0;
static int predictionsListNumSatellitePassesDisplayed = 0;
static int rxIntPart;
static int rxDecPart;
static int txIntPart;
static int txDecPart;
static int predictionsListSelectedSatellite;
static satelliteResults_t currentSatelliteResults;
static int currentlyPredictingSatellite = 0;
static int numTotalSatellitesPredicted = 0;
static satelliteData_t *predictingSat;
static uint32_t nextAlarmBeepTime = 0;
static bool hasRecalculated;
static bool hasSelectedSatellite = false;
satelliteFreq_t currentSatelliteFreqIndex = SATELLITE_VOICE_FREQ;

#if defined(HAS_EMCOM_COLOUR_UI)
// Fork: EMCOM style of the four views (title bar, status strip, key bar), by Marcin SP7TQ.
// Drawing only: the predictions, the keys, the voice prompts and the Doppler retune are shared
// with the classic screen. No static data: everything is built on the stack.
#define EM_STRIP_Y    MENU_HEADER_HEIGHT
#define EM_STRIP_H    11
#define EM_KEYBAR_H   11
#define EM_KEYBAR_Y   (DISPLAY_SIZE_Y - EM_KEYBAR_H)
#define EM_LIST_Y     (EM_STRIP_Y + EM_STRIP_H + 2)
#define EM_ROW_H      14
#define EM_POLAR_CX   100
#define EM_POLAR_CY   ((EM_STRIP_Y + EM_STRIP_H + EM_KEYBAR_Y) / 2)
#define EM_POLAR_R    40

static void emText(int16_t x, int16_t y, const char *s, ucFont_t font, themeItem_t fg, themeItem_t bg)
{
	displayThemeApply(fg, bg);
	displayPrintCore(x, y, s, font, TEXT_ALIGN_LEFT, false);
}

static void emTextRight(int16_t xRight, int16_t y, const char *s, ucFont_t font, themeItem_t fg, themeItem_t bg)
{
	emText((int16_t)(xRight - (int16_t)(strlen(s) * ((font == FONT_SIZE_1) || (font == FONT_SIZE_1_BOLD) ? 6 : 8))), y, s, font, fg, bg);
}

// Title bar with the view number ("1/4") drawn by menuDisplayTitle()
static void emTitle(const char *title)
{
	menuDataGlobal.numItems = NUM_SATELLITE_SCREEN_ITEMS;
	menuDataGlobal.currentItemIndex = displayMode;
	menuDisplayTitle(title);
}

// Coloured field on the status strip, text in the background colour; returns its width
static int16_t emChip(int16_t x, const char *text, themeItem_t colour)
{
	char buf[16];
	size_t n = 0;
	int16_t w;

	for (; text[n] && (n < (sizeof(buf) - 1)); n++)
	{
		char c = text[n];
		buf[n] = (((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c);
	}
	buf[n] = 0;
	w = (int16_t)((n * 6) + 4);

	displayThemeApply(colour, THEME_ITEM_BG_HEADER_TEXT);
	displayFillRect(x, (EM_STRIP_Y + 1), w, 9, false);
	displayThemeApply(colour, THEME_ITEM_BG);
	displayPrintCore((x + 2), (EM_STRIP_Y + 1), buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	return w;
}

static void emStrip(void)
{
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EM_STRIP_Y, DISPLAY_SIZE_X, EM_STRIP_H, false);
}

static void emKeyBar(const char *keys)
{
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EM_KEYBAR_Y, DISPLAY_SIZE_X, EM_KEYBAR_H, false);
	emText(3, (EM_KEYBAR_Y + 2), keys, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayThemeResetToDefault();
}

// "3:12", "1:02:03", "45s"
static void emDuration(char *buf, size_t size, uint32_t secs)
{
	if (secs >= 3600U)
	{
		snprintf(buf, size, "%u:%02u:%02u", (unsigned)(secs / 3600U), (unsigned)((secs / 60U) % 60U), (unsigned)(secs % 60U));
	}
	else if (secs >= 60U)
	{
		snprintf(buf, size, "%u:%02u", (unsigned)(secs / 60U), (unsigned)(secs % 60U));
	}
	else
	{
		snprintf(buf, size, "%us", (unsigned)secs);
	}
}

static int32_t emLocalOffset(void)
{
	return ((nonVolatileSettings.timezone & 0x80) ? (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60)) : 0);
}

// Observer position strip: where it comes from (GPS fix / MANUAL entry) and the short lat / lon
static void emPositionStrip(void)
{
	char buf[20];
	bool fromGps = false;

#if defined(HAS_GPS)
	// With a fix the GPS writes its position to the location setting (gps.c), which the predictions use
	fromGps = ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) && ((gpsData.Status & GPS_STATUS_HAS_FIX) != 0));
#endif

	emStrip();

	if (settingsLocationIsValid())
	{
		double lat = settingsLocationGetLatitude();
		double lon = settingsLocationGetLongitude();
		uint32_t la = (uint32_t)((fabs(lat) * 100.0) + 0.5);
		uint32_t lo = (uint32_t)((fabs(lon) * 100.0) + 0.5);
		int16_t x = (int16_t)(2 + emChip(2, (fromGps ? "GPS" : "MANUAL"), (fromGps ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RSSI_BAR_S9P)) + 4);

		snprintf(buf, sizeof(buf), "%u.%02u%c %u.%02u%c", (unsigned)(la / 100U), (unsigned)(la % 100U), ((lat >= 0.0) ? 'N' : 'S'),
				(unsigned)(lo / 100U), (unsigned)(lo % 100U), ((lon >= 0.0) ? 'E' : 'W'));
		emText(x, (EM_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	}
	else
	{
		emChip(2, "NO POS", THEME_ITEM_FG_TX_COUNTER);
	}

	emTextRight((DISPLAY_SIZE_X - 3), (EM_STRIP_Y + 2), ((emLocalOffset() != 0) ? "LT" : "UTC"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
}

// Header of the predictions list (also while predicting)
static void emListFrame(void)
{
	displayClearBuf();
	emTitle(currentLanguage->satellite);
	emPositionStrip();
	emKeyBar("GREEN OPEN * POS RED EXIT");
}

static void emPredicting(void)
{
	emListFrame();
	emText(4, (EM_LIST_Y + 14), currentLanguage->predicting, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawRect(4, (EM_LIST_Y + 38), (DISPLAY_SIZE_X - 8), 10, true);
	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
	displayFillRect(6, (EM_LIST_Y + 40), (((DISPLAY_SIZE_X - 12) * numTotalSatellitesPredicted) / numSatellitesLoaded), 6, false);
	displayThemeResetToDefault();
	displayRender();
}

// One pass of the list: name, AOS time, maximum elevation. Row 0 is the one GREEN opens.
static void emListRow(int row, satelliteData_t *sat, int passNumber, const struct tm *aos)
{
	char buf[12];
	int16_t y = (int16_t)(EM_LIST_Y + (row * EM_ROW_H));
	bool selected = (row == 0);
	bool visible = ((passNumber == 0) && sat->predictions.isVisible);
	themeItem_t bg = (selected ? THEME_ITEM_BG_MENU_ITEM_SELECTED : THEME_ITEM_BG);

	if (selected)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRect(0, y, DISPLAY_SIZE_X, (EM_ROW_H - 1), false);
	}

	// marker: the row GREEN opens, or a satellite in view now
	if (selected || visible)
	{
		displayThemeApply((selected ? THEME_ITEM_BG : THEME_ITEM_FG_SATELLITE_COLOUR), bg);
		displayFillRect(0, (y + 3), 2, (EM_ROW_H - 7), false);
	}

	displayThemeApply((selected ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT), bg);
	snprintf(buf, 9, "%s", sat->name); // 8 characters
	displayPrintCore(6, (y + 3), buf, FONT_SIZE_2, TEXT_ALIGN_LEFT, false);

	if (visible)
	{
		snprintf(buf, sizeof(buf), "NOW");
	}
	else
	{
		snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)aos->tm_hour, (unsigned)aos->tm_min);
	}
	emText(78, (y + 3), buf, FONT_SIZE_2, (selected ? THEME_ITEM_BG : (visible ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RX_FREQ)), bg);

	snprintf(buf, sizeof(buf), "%u%c", (unsigned)satelliteGetMaximumElevation(sat, passNumber), 176);
	emTextRight((DISPLAY_SIZE_X - 3), (y + 3), buf, FONT_SIZE_2, (selected ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT), bg);
}

// Countdown of the current / next pass of the active satellite: "AOS in 3:12", "LOS in 5:40"
static themeItem_t emPassState(char *buf, size_t size)
{
	satellitePass_t *p = &currentActiveSatellite->predictions.passes[0];
	time_t_custom now = uiDataGlobal.dateTimeSecs;
	char t[12];

	if ((p->valid != PREDICTION_RESULT_OK) || (now > p->satelliteLOS))
	{
		snprintf(buf, size, "%s: %s", currentLanguage->pass, currentLanguage->none);
		return THEME_ITEM_FG_DECORATION;
	}

	if (now < p->satelliteAOS)
	{
		emDuration(t, sizeof(t), (p->satelliteAOS - now));
		snprintf(buf, size, "AOS in %s", t);
		return THEME_ITEM_FG_RSSI_BAR_S9P;
	}

	emDuration(t, sizeof(t), (p->satelliteLOS - now));
	snprintf(buf, size, "LOS in %s", t);
	return THEME_ITEM_FG_RSSI_BAR;
}

static void emFreqLine(int16_t y, const char *label, uint32_t freqHz, themeItem_t colour, const char *noFreq)
{
	char buf[16];

	emText(4, (y + 5), label, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	if (freqHz != 0)
	{
		uint32_t f = (freqHz / 10U); // 10 Hz units

		snprintf(buf, sizeof(buf), "%u.%05u", (unsigned)(f / 100000U), (unsigned)(f % 100000U));
		emText(20, y, buf, FONT_SIZE_3, colour, THEME_ITEM_BG);
	}
	else
	{
		emText(20, (y + 5), noFreq, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	}
}

// Selected satellite: azimuth / elevation, Doppler corrected RX / TX, pass countdown
static void emSelected(const char *freqName)
{
	char buf[24];
	int16_t y;
	uint8_t powerLevel = trxGetPowerLevel();
	satellite_txRxFreqs *f = &currentSatelliteResults.freqs[currentSatelliteFreqIndex];

	displayClearBuf();
	emTitle(currentActiveSatellite->name);

	emStrip();
	int16_t x = (int16_t)(2 + emChip(2, freqName, THEME_ITEM_FG_RX_FREQ) + 4);
	emText(x, (EM_STRIP_Y + 2), "DOPPLER", FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	snprintf(buf, sizeof(buf), "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
	emTextRight((DISPLAY_SIZE_X - 3), (EM_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);

	// AZ / EL
	y = (EM_STRIP_Y + EM_STRIP_H + 5);
	emText(4, (y + 5), currentLanguage->azimuth, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	snprintf(buf, sizeof(buf), "%d%c", currentSatelliteResults.azimuthAsInteger, 176);
	emText(20, y, buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	emText(84, (y + 5), currentLanguage->elevation, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	snprintf(buf, sizeof(buf), "%d%c", currentSatelliteResults.elevationAsInteger, 176);
	emText(100, y, buf, FONT_SIZE_3, ((currentSatelliteResults.elevationAsInteger > 0) ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_DECORATION), THEME_ITEM_BG);

	// Frequencies
	y += 23;
	emFreqLine(y, "RX", f->rxFreq, THEME_ITEM_FG_RX_FREQ, "---");
	y += 19;
	emFreqLine(y, "TX", ((f->rxFreq != 0) ? f->txFreq : 0), THEME_ITEM_FG_TX_FREQ, ((f->rxFreq != 0) ? "RX only" : "---"));

	// Pass
	y += 23;
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, (y - 3), DISPLAY_SIZE_X, true);
	themeItem_t colour = emPassState(buf, sizeof(buf));
	emText(4, y, buf, FONT_SIZE_1, colour, THEME_ITEM_BG);
	if (currentActiveSatellite->predictions.passes[0].valid == PREDICTION_RESULT_OK)
	{
		snprintf(buf, sizeof(buf), "%s %u%c", currentLanguage->maximum, (unsigned)satelliteGetMaximumElevation(currentActiveSatellite, 0), 176);
		emTextRight((DISPLAY_SIZE_X - 3), y, buf, FONT_SIZE_1, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	}
	snprintf(buf, sizeof(buf), "RSSI %ddBm", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	emText(4, (y + 10), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

#if defined(HAS_SSTV_MENU) && defined(I2S_APRS_RX)
	emKeyBar("1-3 QRG  9 SSTV  RED LIST");
#else
	emKeyBar("1-3 QRG  # NEXT  RED LIST");
#endif
	displayRender();
}

// Polar view, static part: frame, sky plot grid, left column, pass track (drawn with the classic maths)
static void emPolarFrame(void)
{
	displayClearBuf();
	emTitle(currentActiveSatellite->name);
	emStrip();
	emKeyBar("UP/DN VIEW # NEXT RED LIST");

	displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);
	displayDrawCircle(EM_POLAR_CX, EM_POLAR_CY, EM_POLAR_R, true);
	displayDrawCircle(EM_POLAR_CX, EM_POLAR_CY, ((EM_POLAR_R / 3) * 2), true);
	displayDrawCircle(EM_POLAR_CX, EM_POLAR_CY, (EM_POLAR_R / 3), true);
	displayDrawFastVLine(EM_POLAR_CX, (EM_POLAR_CY - (EM_POLAR_R + 3)), ((EM_POLAR_R + 3) * 2), true);
	displayDrawFastHLine((EM_POLAR_CX - (EM_POLAR_R + 3)), EM_POLAR_CY, ((EM_POLAR_R + 3) * 2), true);
	displayThemeResetToDefault();
}

// Left column of the polar view: label (small) + value (big)
static void emPolarValue(int16_t y, const char *label, const char *value, themeItem_t colour)
{
	emText(3, y, label, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	emText(3, (y + 9), value, ((strlen(value) > 6) ? FONT_SIZE_1 : FONT_SIZE_3), colour, THEME_ITEM_BG);
	displayThemeResetToDefault(); // the pass track is drawn next, in the default colour
}

// Polar view, live part: status strip (pass state, RSSI) and the S-meter column on the right
static void emPolarLive(void)
{
	char buf[24];
	int rssi = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	int16_t h = (int16_t)(EM_KEYBAR_Y - (EM_STRIP_Y + EM_STRIP_H) - 2);
	int lvl = MIN(MAX(0, (rssi - SMETER_S0)), (SMETER_S9 - SMETER_S0));

	emStrip();
	if (currentActiveSatellite->predictions.numPasses > 0)
	{
		snprintf(buf, sizeof(buf), "%s %u/%u", currentLanguage->pass, (unsigned)(currentActiveSatellite->predictions.selectedPassNumber + 1), (unsigned)currentActiveSatellite->predictions.numPasses);
		emChip(2, buf, THEME_ITEM_FG_RX_FREQ);
	}
	snprintf(buf, sizeof(buf), "%ddBm", rssi);
	emTextRight((DISPLAY_SIZE_X - 3), (EM_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);

	lvl = ((lvl * h) / (SMETER_S9 - SMETER_S0));
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect((DISPLAY_SIZE_X - 6), (EM_STRIP_Y + EM_STRIP_H + 1), 5, (h - lvl), false);
	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
	displayFillRect((DISPLAY_SIZE_X - 6), ((EM_STRIP_Y + EM_STRIP_H + 1) + (h - lvl)), 5, lvl, false);
	displayThemeResetToDefault();
}

// Pass details view
static void emPrediction(satellitePass_t *pass)
{
	char buf[24];
	satelliteData_t *sat = currentActiveSatellite;
	int16_t y = (EM_STRIP_Y + EM_STRIP_H + 4);

	displayClearBuf();
	emTitle(sat->name);
	emStrip();

	if (sat->predictions.numPasses > 0)
	{
		snprintf(buf, sizeof(buf), "%s %u/%u", currentLanguage->pass, (unsigned)(sat->predictions.selectedPassNumber + 1), (unsigned)sat->predictions.numPasses);
		emChip(2, buf, THEME_ITEM_FG_RX_FREQ);
	}
	emTextRight((DISPLAY_SIZE_X - 3), (EM_STRIP_Y + 2), ((emLocalOffset() != 0) ? "LT" : "UTC"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);

	if (pass->valid == PREDICTION_RESULT_OK)
	{
		struct tm t;
		time_t_custom tt;

		for (int i = 0; i < 2; i++)
		{
			tt = (((i == 0) ? pass->satelliteAOS : pass->satelliteLOS) + emLocalOffset());
			gmtime_r_Custom(&tt, &t);
			emText(4, (y + 5), ((i == 0) ? "AOS" : "LOS"), FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)t.tm_hour, (unsigned)t.tm_min, (unsigned)t.tm_sec);
			emText(28, y, buf, FONT_SIZE_3, ((i == 0) ? THEME_ITEM_FG_RX_FREQ : THEME_ITEM_FG_DEFAULT), THEME_ITEM_BG);
			y += 22;
		}

		emText(4, (y + 5), currentLanguage->maximum, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		snprintf(buf, sizeof(buf), "%u%c", (unsigned)satelliteGetMaximumElevation(sat, sat->predictions.selectedPassNumber), 176);
		emText(28, y, buf, FONT_SIZE_3, THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);

		emDuration(buf, sizeof(buf), pass->satellitePassDuration);
		emTextRight((DISPLAY_SIZE_X - 4), y, buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		emText(84, (y + 5), "DUR", FONT_SIZE_1, THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	}
	else if ((pass->valid == PREDICTION_RESULT_LIMIT) && (sat->predictions.numPasses == 0))
	{
		emText(4, (y + 12), currentLanguage->list_empty, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	}

	emKeyBar("</> PASS  SK2+GRN ALARM");
	displayRender();
}
#endif

menuStatus_t menuSatelliteScreen(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		struct tm buildDateTime;

		predictionsListSelectedSatellite = 0;
		currentlyPredictingSatellite = 0;

		currentPrediction.state = PREDICTION_STATE_NONE;
		currentlyDisplayedListPosition = 0;

		if (!hasSelectedSatellite)
		{
			if (settingsLocationIsValid() == false)
			{
				// Use the quickkeys system to enter the Radio Infos screen at a specific page
				menuSystemPopPreviousMenu();
				menuSystemPushNewMenu(MENU_RADIO_INFOS);
				ev->hasEvent = true;
				ev->function = QUICKKEY_MENUVALUE(MENU_RADIO_INFOS, RADIO_INFOS_LOCATION, 0);
				ev->events = FUNCTION_EVENT;
				menuSystemCallCurrentMenuTick(ev);
				return MENU_STATUS_SUCCESS;
			}

			memset(&buildDateTime, 0x00, sizeof(struct tm));// clear entire struct
			buildDateTime.tm_mday = BUILD_DAY;  /* day of the month, 1 to 31 */
			buildDateTime.tm_mon = BUILD_MONTH - 1;   /* months since January, 0 to 11 */
			buildDateTime.tm_year = BUILD_YEAR - 1900;  /* years since 1900 */

			time_t_custom buildYYMMDDasUtcTime = mktime_custom(&buildDateTime) - (48 * 3600);// subtract 2 days, in case of problems between build TZ and TZ of user. 2 days is conservative in case of strange TZ's

			if (uiDataGlobal.dateTimeSecs < buildYYMMDDasUtcTime)
			{
				// Use the quickkeys system to enter the Radio Infos screen at a specific page
				menuSystemPopPreviousMenu();
				menuSystemPushNewMenu(MENU_RADIO_INFOS);
				ev->hasEvent = true;
				ev->function = QUICKKEY_MENUVALUE(MENU_RADIO_INFOS, RADIO_INFOS_DATE, 0);
				ev->events = FUNCTION_EVENT;
				menuSystemCallCurrentMenuTick(ev);
				return MENU_STATUS_SUCCESS;
			}

			if (!hasSatelliteKeps)
			{
				loadKeps();
			}

			if (hasSatelliteKeps) // No Keps data, no computation
			{
				// user may have changed the location
				satelliteSetObserverLocation(settingsLocationGetLatitude(), settingsLocationGetLongitude(),
						0);// Use zero for height, as this seems to make virtually no difference to the calculations. We may however need to change this to some more average height for the ham radio population

				aprsBeaconingPrepareSatelliteConfig();
			}

			satelliteChannelData.rxFreq = 0;
			satelliteChannelData.txFreq = 0;
			satelliteChannelData.rxTone = CODEPLUG_CSS_TONE_NONE;
			satelliteChannelData.chMode = RADIO_MODE_NONE;
			codeplugChannelSetFlag(&satelliteChannelData, CHANNEL_FLAG_BW_25K, 1);
		}
		else if (hasSatelliteKeps && settingsLocationIsValid())
		{
			// Fork: back from the location page ('*' on the list) or another screen: the position
			// may have changed (the predictions were cleared then and are made again from here).
			satelliteSetObserverLocation(settingsLocationGetLatitude(), settingsLocationGetLongitude(), 0);
		}

		currentChannelData = &satelliteChannelData;// Now make it available to the rest of the firmware e.g the Tx screen
		trxSetModeAndBandwidth(satelliteChannelData.chMode, true);
		trxSetRxCSS(RADIO_DEVICE_PRIMARY, satelliteChannelData.rxTone);// Never any Rx CTCSS

		currentActiveSatellite = &satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite];
		menuSatelliteScreenNextUpdateTime = (ev->time - 1U);

		menuSystemRegisterExitCallback(exitCallback, NULL);

		calculateActiveSatelliteData(true);
		nextCalculationTime = (ev->time + 1000U); // 1000 milliseconds

		updateScreen(ev, true, true);
	}
	else
	{
		if (hasSatelliteKeps)
		{
			if (numTotalSatellitesPredicted == 0)
			{
				clockManagerSetRunMode(kAPP_PowerModeHsrun, CLOCK_MANAGER_SPEED_HS_RUN);
			}

			while (!calculatePredictionsForSatelliteIndex(currentlyPredictingSatellite))
			{
				vTaskDelay((0U / portTICK_PERIOD_MS));
			}

			predictingSat = &satelliteDataNative[currentlyPredictingSatellite];

			if (numTotalSatellitesPredicted < numSatellitesLoaded)
			{
				if (predictingSat->predictions.passes[predictingSat->predictions.numPassBeingPredicted].valid == PREDICTION_RESULT_LIMIT)
				{
					currentlyPredictingSatellite++;
					numTotalSatellitesPredicted++;

					updateScreen(ev, false, true);
					if (numTotalSatellitesPredicted == numSatellitesLoaded)
					{
						if (settingsIsOptionBitSet(BIT_SATELLITE_MANUAL_AUTO))
						{
							// need to set the current satellite to the new first satellite in the predictions list.
							int foundSatellite = menuSatelliteFindNextSatellite();
							if (foundSatellite != -1)
							{
								selectSatellite(foundSatellite);
							}
						}
						clockManagerSetRunMode(kAPP_PowerModeHsrun,CLOCK_MANAGER_SPEED_RUN);
					}
				}
			}
			else
			{
				// keep sweeping in case any of the satellites goes LOS and the predictions need to be re-run.
				currentlyPredictingSatellite = ((currentlyPredictingSatellite + 1) % numSatellitesLoaded);
			}

			if (ev->time > nextCalculationTime)
			{
				calculateActiveSatelliteData(false);
				nextCalculationTime = ev->time + 1000; // 1000 milliseconds
			}

			if ((menuSatelliteScreenNextUpdateTime != 0) && (ev->time > menuSatelliteScreenNextUpdateTime))
			{
				updateScreen(ev, false, false);
			}

			if ((uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_SATELLITE) &&
				(uiDataGlobal.dateTimeSecs >= uiDataGlobal.SatelliteAndAlarmData.alarmTime))
			{
				if (ev->time > nextAlarmBeepTime)
				{
					if  (uiDataGlobal.dateTimeSecs >= (uiDataGlobal.SatelliteAndAlarmData.alarmTime + ALARM_OFFSET_SECS))
					{
						// alarm has been beeping for ALARM_OFFSET_SECS and has not been cancelled.
						// So presume the radio is unattended, turn the alarm off, and put it back into suspend.
						uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_CANCELLED;
#if ! defined(PLATFORM_MD9600)
						powerOffFinalStage(true, false);
#endif
					}
					else
					{
						soundSetMelody(MELODY_QUICKKEYS_CLEAR_ACK_BEEP);
					}

					nextAlarmBeepTime = ev->time + 1000;
				}
			}
		}

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}
	return MENU_STATUS_SUCCESS;
}

static void updateScreen(uiEvent_t *ev, bool firstRun, bool announceVP)
{
	if (hasSatelliteKeps == false)
	{
		if (firstRun)
		{
			displayClearBuf();

			menuDisplayTitle(currentLanguage->satellite);
			displayPrintCentered((DISPLAY_SIZE_Y / 2) - 6, currentLanguage->list_empty, FONT_SIZE_2);

			voicePromptsInit();
			voicePromptsAppendLanguageString(currentLanguage->satellite);
			voicePromptsAppendLanguageString(currentLanguage->list_empty);
			voicePromptsPlay();

#if defined(HAS_EMCOM_COLOUR_UI)
			if (emcomStyleEnabled())
			{
				emKeyBar("RED EXIT");
			}
#endif
			displayRender();
		}

		return;
	}
	else
	{
		char buffer[SCREEN_LINE_BUFFER_SIZE];
#if defined(HAS_COLOURS)
		bool dblHeight = settingsIsOptionBitSet(BIT_UI_USES_DOUBLE_HEIGHT);
#else
		const bool dblHeight = false;
#endif

		// prevent all annoucements when returning from the Tx screen
		if (menuSystemGetPreviouslyPushedMenuNumber(false) == UI_TX_SCREEN)
		{
			announceVP = false;
		}

#if defined(HAS_EMCOM_COLOUR_UI)
		const bool emcom = emcomStyleEnabled();
#endif

		if (firstRun
#if defined(HAS_EMCOM_COLOUR_UI)
				&& (emcom == false) // every EMCOM view draws its whole screen
#endif
		)
		{
			displayClearBuf();

			// Do not display the menu title if a list of passes is available, as that list uses the
			// whole screen (and the screen flickers on the MD*-3x0)
			if ((displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST) &&
					((numTotalSatellitesPredicted != numSatellitesLoaded) || (predictionsListNumSatellitePassesDisplayed == 0)))
			{
				menuDisplayTitle(currentLanguage->satellite);
			}

			displayRender();
		}

		if (numTotalSatellitesPredicted != numSatellitesLoaded)
		{
#if defined(HAS_EMCOM_COLOUR_UI)
			if (emcom)
			{
				emPredicting();
			}
			else
#endif
			{
				displayClearRows(2, 6, false);

				displayPrintCentered(16, currentLanguage->predicting, FONT_SIZE_3);
				displayDrawRect(2,
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
						(DISPLAY_SIZE_Y / 4)
#else
						(DISPLAY_SIZE_Y / 2)
#endif
						, DISPLAY_SIZE_X - 2, 12, true);
				displayFillRect(3,
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
						(DISPLAY_SIZE_Y / 4)
#else
						(DISPLAY_SIZE_Y / 2)
#endif
						, ((DISPLAY_SIZE_X - 2) * numTotalSatellitesPredicted) / numSatellitesLoaded, 12, false);

				displayRenderRows(2, 6);
			}
			menuSatelliteScreenNextUpdateTime = 0;// Don't do time based update

			if (firstRun)
			{
				bool wasPlaying = voicePromptsIsPlaying();
				voicePromptsInit();
				if (!wasPlaying)
				{
					voicePromptsAppendLanguageString(currentLanguage->satellite);
				}
				voicePromptsAppendLanguageString(currentLanguage->predicting);
				voicePromptsPlay();
			}
			return;
		}

		// Don't stop/cut the current VP
		if (announceVP && voicePromptsIsPlaying())
		{
			announceVP = false;
		}

		switch(displayMode)
		{
			case SATELLITE_SCREEN_SELECTED_SATELLITE:
			{
				const char *freqNames[] = { currentLanguage->voice_prompt_level_1, currentLanguage->APRS, "CW Rx" };// Temporary hard coded names, will eventually need new language strings

#if defined(HAS_EMCOM_COLOUR_UI)
				if (emcom)
				{
					if (firstRun || hasRecalculated || announceVP)
					{
						emSelected(freqNames[currentSatelliteFreqIndex]);
					}
				}
				else
#endif
				{
					if(hasRecalculated || announceVP)
					{
						displayClearBuf();
					}

					uiUtilityRenderHeader(false, false, ((ev->buttons & BUTTON_SK1) == BUTTON_SK1));

					if (hasRecalculated || announceVP)
					{
						// calculate these now for display later...
						snprintf(azelBuffer, SCREEN_LINE_BUFFER_SIZE, "%s:%3d%c %s:%3d%c", currentLanguage->azimuth, currentSatelliteResults.azimuthAsInteger, 176, currentLanguage->elevation, currentSatelliteResults.elevationAsInteger, 176);
						displayPrintCoreDoubleHeight(0, (DISPLAY_SIZE_Y / 4), currentActiveSatellite->name, FONT_SIZE_2, TEXT_ALIGN_LEFT, false, dblHeight);

						displayPrintCoreDoubleHeight(0, (DISPLAY_SIZE_Y / 4), freqNames[currentSatelliteFreqIndex], FONT_SIZE_2, TEXT_ALIGN_RIGHT, false, dblHeight);

						displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2)
#if defined(PLATFORM_RD5R)
								- 4
#else
								- 6
#endif
								, azelBuffer, FONT_SIZE_2, dblHeight);

						if (currentSatelliteResults.freqs[currentSatelliteFreqIndex].rxFreq != 0)
						{
							rxIntPart = currentSatelliteResults.freqs[currentSatelliteFreqIndex].rxFreq / 1E6;
							rxDecPart = (currentSatelliteResults.freqs[currentSatelliteFreqIndex].rxFreq - (rxIntPart * 1E6)) / 10;
							snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "R:%3d.%05u",rxIntPart,rxDecPart);
							displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2) + 6 + (dblHeight ? FONT_SIZE_2_HEIGHT : 0), buffer, FONT_SIZE_2, dblHeight);

							if (currentSatelliteResults.freqs[currentSatelliteFreqIndex].txFreq != 0)
							{
								txIntPart = currentSatelliteResults.freqs[currentSatelliteFreqIndex].txFreq / 1E6;
								txDecPart = (currentSatelliteResults.freqs[currentSatelliteFreqIndex].txFreq - (txIntPart * 1E6)) / 10;

								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "T:%3d.%05u",txIntPart,txDecPart);
								displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2) + 16 + (dblHeight ? (FONT_SIZE_2_HEIGHT * 2) : 0), buffer, FONT_SIZE_2, dblHeight);
							}
						}

						displayRender();// render the whole screen;
					}
					else
					{
						displayRenderRows(0, 2);
					}
				}

				if (announceVP || hasRecalculated)
				{
					char vpBuffer[SCREEN_LINE_BUFFER_SIZE];

					if (menuSystemGetPreviouslyPushedMenuNumber(false) != UI_TX_SCREEN)
					{
						if (announceVP)
						{
							voicePromptsInit();
							voicePromptsAppendString(currentActiveSatellite->name);

							if (currentSatelliteFreqIndex == SATELLITE_OTHER_FREQ)
							{
								voicePromptsAppendString(freqNames[currentSatelliteFreqIndex]);
							}
							else
							{
								voicePromptsAppendLanguageString(freqNames[currentSatelliteFreqIndex]);
							}

							voicePromptsAppendLanguageString(currentLanguage->azimuth);
							snprintf(vpBuffer, SCREEN_LINE_BUFFER_SIZE, "%3d%c", currentSatelliteResults.azimuthAsInteger, 176);
							voicePromptsAppendString(vpBuffer);
							voicePromptsAppendLanguageString(currentLanguage->elevation);
							snprintf(vpBuffer, SCREEN_LINE_BUFFER_SIZE, "%3d%c", currentSatelliteResults.elevationAsInteger, 176);
							voicePromptsAppendString(vpBuffer);

							voicePromptsPlay();
						}
					}
				}

				menuSatelliteScreenNextUpdateTime = (ev->time + 250U);
			}
			break;

			case SATELLITE_SCREEN_SELECTED_SATELLITE_POLAR:
			{
				satellitePass_t *displayedPredictionPass  = &currentActiveSatellite->predictions.passes[currentActiveSatellite->predictions.selectedPassNumber];
				const int DOT_RADIUS = 2;
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				const int MAX_RADIUS = 44;
#else
				const int MAX_RADIUS = ((DISPLAY_SIZE_Y / 2) - 4);
#endif
				float az, elFactor, s, c, lastX = 0, lastY = 0, x, y;
				satelliteResults_t results;
				int16_t satX = INT16_MIN, satY = INT16_MIN;
				time_t_custom displayedPassTimeDiff = displayedPredictionPass->satelliteAOS - uiDataGlobal.dateTimeSecs;
#define POLAR_GRAPHICS_X_OFFSET  24
				int polarCX = (POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2));
				int polarCY = (DISPLAY_SIZE_Y / 2);
				int polarR = MAX_RADIUS;
#if defined(HAS_EMCOM_COLOUR_UI)
				if (emcom)
				{
					polarCX = EM_POLAR_CX;
					polarCY = EM_POLAR_CY;
					polarR = EM_POLAR_R;
				}
#endif

				if (displayedPredictionPass->valid == PREDICTION_RESULT_OK)
				{
					volatile uint32_t startTime;

					if(hasRecalculated || announceVP)
					{
						struct tm dispTimeAndDate;

						 // below 1 minute displayedPassTimeDiff is used.
						if (displayedPassTimeDiff >= 60)
						{
							gmtime_r_Custom(&displayedPassTimeDiff, &dispTimeAndDate);
						}

#if defined(HAS_EMCOM_COLOUR_UI)
						if (emcom)
						{
							emPolarFrame();
						}
						else
#endif
						{
							displayClearBuf();

							snprintf(buffer, 8, "%s", currentActiveSatellite->name);
							displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
							displayPrintAtDoubleHeight(4, (DISPLAY_SIZE_Y / 2) - 4 - (dblHeight ? (FONT_SIZE_2_HEIGHT / 2) : 0), buffer, FONT_SIZE_2, dblHeight);
						}

						if (announceVP)
						{
							voicePromptsAppendString(currentActiveSatellite->name);
						}

#if defined(HAS_EMCOM_COLOUR_UI)
						if (emcom == false)
#endif
						{
							displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);
							displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), DISPLAY_SIZE_Y / 2, MAX_RADIUS, true);
							displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), DISPLAY_SIZE_Y / 2, (MAX_RADIUS / 3 ) * 2, true);
							displayDrawCircle(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2), DISPLAY_SIZE_Y / 2, (MAX_RADIUS / 3 ), true);

							displayDrawFastVLine(POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2),
									(DISPLAY_SIZE_Y / 2) - (MAX_RADIUS + 4), (MAX_RADIUS + 4) * 2, true);
							displayDrawFastHLine((POLAR_GRAPHICS_X_OFFSET + (DISPLAY_SIZE_X / 2)) - (MAX_RADIUS + 4),
									(DISPLAY_SIZE_Y / 2), (MAX_RADIUS + 4) * 2, true);
							displayThemeResetToDefault();
						}

						if ((uiDataGlobal.dateTimeSecs < displayedPredictionPass->satelliteAOS) || (uiDataGlobal.dateTimeSecs > displayedPredictionPass->satelliteLOS))
						{
							startTime = displayedPredictionPass->satelliteAOS;

#if defined(HAS_EMCOM_COLOUR_UI)
							if (emcom)
							{
								bool before = (uiDataGlobal.dateTimeSecs < displayedPredictionPass->satelliteAOS);

								if (before)
								{
									emDuration(buffer, SCREEN_LINE_BUFFER_SIZE, displayedPassTimeDiff);
								}
								else
								{
									buffer[0] = '-';
									buffer[1] = 0;
								}
								emPolarValue((EM_STRIP_Y + EM_STRIP_H + 6), "AOS in", buffer, THEME_ITEM_FG_RSSI_BAR_S9P);
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u%c", (unsigned)satelliteGetMaximumElevation(currentActiveSatellite, currentActiveSatellite->predictions.selectedPassNumber), 176);
								emPolarValue((EM_STRIP_Y + EM_STRIP_H + 36), currentLanguage->maximum, buffer, THEME_ITEM_FG_RSSI_BAR);
							}
							else
#endif
							{
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s:%02d%c", currentLanguage->maximum, satelliteGetMaximumElevation(currentActiveSatellite, currentActiveSatellite->predictions.selectedPassNumber), 176);
								displayPrintAtDoubleHeight(4, (DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) - 4 - (dblHeight ? (FONT_SIZE_3_HEIGHT) : 0) , buffer, FONT_SIZE_3, dblHeight);

								if (displayedPassTimeDiff >= 60)
								{
									if (displayedPassTimeDiff >= 3600)
									{
										snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "-%u:%02u:%02u", dispTimeAndDate.tm_hour, dispTimeAndDate.tm_min, dispTimeAndDate.tm_sec);
										displayPrintAtDoubleHeight(((dispTimeAndDate.tm_hour > 9) ? 2 : 4), 4, buffer, FONT_SIZE_2, dblHeight);
									}
									else
									{
										// between 60 and 3599 seconds
										snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "-%u:%02u", dispTimeAndDate.tm_min, dispTimeAndDate.tm_sec);
										displayPrintAtDoubleHeight(8, 4, buffer, FONT_SIZE_2, dblHeight);
									}
								}
								else
								{
									snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "-%02us", displayedPassTimeDiff);
									displayPrintAtDoubleHeight(12, 4, buffer, FONT_SIZE_3, dblHeight);
								}
							}

							if (announceVP)
							{
								voicePromptsAppendLanguageString(currentLanguage->inHHMMSS);

								if (displayedPassTimeDiff >= 60)
								{
									if (displayedPassTimeDiff >= 3600)
									{
										voicePromptsAppendInteger(dispTimeAndDate.tm_hour);
										voicePromptsAppendLanguageString(currentLanguage->hours);

										voicePromptsAppendInteger(dispTimeAndDate.tm_min);
										voicePromptsAppendPrompt(PROMPT_MINUTES);
									}
									else
									{
										// between 60 and 3599 seconds
										voicePromptsAppendInteger(dispTimeAndDate.tm_min);
										voicePromptsAppendPrompt(PROMPT_MINUTES);

										voicePromptsAppendInteger(dispTimeAndDate.tm_sec);
										voicePromptsAppendPrompt(PROMPT_SECONDS);
									}
								}
								else
								{
									voicePromptsAppendInteger(displayedPassTimeDiff);
									voicePromptsAppendPrompt(PROMPT_SECONDS);
								}

								voicePromptsAppendLanguageString(currentLanguage->maximum);
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE,"%d%c", satelliteGetMaximumElevation(currentActiveSatellite, currentActiveSatellite->predictions.selectedPassNumber), 176);
								voicePromptsAppendString(buffer);
							}
						}
						else
						{
							startTime = uiDataGlobal.dateTimeSecs;
#if defined(HAS_EMCOM_COLOUR_UI)
							if (emcom)
							{
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%c", currentSatelliteResults.azimuthAsInteger, 176);
								emPolarValue((EM_STRIP_Y + EM_STRIP_H + 6), currentLanguage->azimuth, buffer, THEME_ITEM_FG_DEFAULT);
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%c", currentSatelliteResults.elevationAsInteger, 176);
								emPolarValue((EM_STRIP_Y + EM_STRIP_H + 36), currentLanguage->elevation, buffer, THEME_ITEM_FG_RSSI_BAR);
							}
							else
#endif
							{
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s:%03d%c", currentLanguage->azimuth, currentSatelliteResults.azimuthAsInteger, 176);
								displayPrintAtDoubleHeight(4, 6, buffer, FONT_SIZE_3, dblHeight);
							}

							if (announceVP)
							{
								voicePromptsAppendLanguageString(currentLanguage->azimuth);
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%3d%c", currentSatelliteResults.azimuthAsInteger, 176);
								voicePromptsAppendString(buffer);
							}

#if defined(HAS_EMCOM_COLOUR_UI)
							if (emcom == false)
#endif
							{
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s:%02u%c", currentLanguage->elevation, currentSatelliteResults.elevationAsInteger, 176);
								displayPrintAtDoubleHeight(4, (DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) - 4 - (dblHeight ? (FONT_SIZE_3_HEIGHT) : 0), buffer, FONT_SIZE_3, dblHeight);
							}

							if (announceVP)
							{
								voicePromptsAppendLanguageString(currentLanguage->elevation);
								snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2d%c", currentSatelliteResults.elevationAsInteger, 176);
								voicePromptsAppendString(buffer);
							}
						}

						for(uint32_t t = startTime; t < displayedPredictionPass->satelliteLOS; t += 30)
						{
							satelliteCalculateForDateTimeSecs(currentActiveSatellite, t, &results, SATELLITE_PREDICTION_LEVEL_TIME_EL_AND_AZ);
							az = deg2rad(results.azimuthAsInteger);
							elFactor = (1 - (fabs(results.elevationAsInteger) / 90)) * polarR;
							s = sin(az);
							c = cos(az);
							x = polarCX + (s * elFactor);
							y = polarCY - (c * elFactor);// NOTE... Subtract value from Y center - for North at top of screen

							if (t == startTime)
							{
								// Postpone satellite spot drawing, to avoid the path drawn on top of it
								satX = (int16_t)x;
								satY = (int16_t)y;
							}
							else
							{
								displayDrawLine((int16_t)lastX, (int16_t)lastY, (int16_t)x, (int16_t)y, true);
							}

							lastX = x;
							lastY = y;
						}

						if ((satX != INT16_MIN) && (satY != INT16_MIN))
						{
							displayThemeApply(THEME_ITEM_FG_SATELLITE_COLOUR, THEME_ITEM_BG);
							displayFillCircle(satX, satY, (DOT_RADIUS + 1), true);
							displayThemeResetToDefault();
						}
					}

#if defined(HAS_EMCOM_COLOUR_UI)
					if (emcom)
					{
						emPolarLive();
					}
					else
#endif
					{
						const uint32_t S_METER_BAR_WIDTH = 8;
						// draw S meter with range S0 to S9.
						int rssi = MIN(MAX(0, trxGetRSSIdBm(RADIO_DEVICE_PRIMARY) - SMETER_S0), (SMETER_S9 - SMETER_S0));

						rssi = (rssi * DISPLAY_SIZE_Y) / (SMETER_S9 - SMETER_S0);

						displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
						displayFillRect(DISPLAY_SIZE_X - S_METER_BAR_WIDTH, 0                     , S_METER_BAR_WIDTH, DISPLAY_SIZE_Y - rssi  , true);
						displayFillRect(DISPLAY_SIZE_X - S_METER_BAR_WIDTH, DISPLAY_SIZE_Y - rssi , S_METER_BAR_WIDTH, rssi                   , false);
						displayThemeResetToDefault();
					}

					menuSatelliteScreenNextUpdateTime = (ev->time + 250U);
				}
				else
				{
					char buf[SCREEN_LINE_BUFFER_SIZE];

					snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s: %s", currentLanguage->pass, currentLanguage->none);
#if defined(HAS_EMCOM_COLOUR_UI)
					if (emcom)
					{
						displayClearBuf();
						emTitle(currentActiveSatellite->name);
						emStrip();
						emText(4, ((DISPLAY_SIZE_Y / 2) - 8), buf, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
						emKeyBar("UP/DN VIEW # NEXT RED LIST");
					}
					else
#endif
					{
						displayClearBuf();
						displayPrintCentered(4, currentActiveSatellite->name, FONT_SIZE_3);
						displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2) + 4, buf, FONT_SIZE_3, dblHeight);
					}

					if (announceVP)
					{
						voicePromptsAppendString(currentActiveSatellite->name);
						voicePromptsAppendPrompt(PROMPT_SILENCE);
						voicePromptsAppendLanguageString(currentLanguage->pass);
						voicePromptsAppendLanguageString(currentLanguage->none);
					}

					menuSatelliteScreenNextUpdateTime = 0;// don't update again
				}

				if (announceVP)
				{
					voicePromptsPlay();
				}

				displayRender();
			}
			break;

			case SATELLITE_SCREEN_SELECTED_SATELLITE_PREDICTION:
			{
				satellitePass_t *displayedPredictionPass  = &currentActiveSatellite->predictions.passes[currentActiveSatellite->predictions.selectedPassNumber];

#if defined(HAS_EMCOM_COLOUR_UI)
				const bool classicDraw = (emcom == false);
#else
				const bool classicDraw = true;
#endif

				if (classicDraw)
				{
					displayClearBuf();
					displayPrintCenteredDoubleHeight(4, currentActiveSatellite->name, FONT_SIZE_3, dblHeight);
				}
				voicePromptsInit();

				if (currentActiveSatellite->predictions.numPasses > 0)
				{
					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s %u / %u", currentLanguage->pass, (currentActiveSatellite->predictions.selectedPassNumber + 1), currentActiveSatellite->predictions.numPasses);
					if (classicDraw)
					{
						displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 4) + 4 + (dblHeight ? (FONT_SIZE_3_HEIGHT / 3) : 0), buffer, FONT_SIZE_2, dblHeight);
					}

					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						voicePromptsAppendString(currentActiveSatellite->name);
						voicePromptsAppendLanguageString(currentLanguage->pass);
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u", (currentActiveSatellite->predictions.selectedPassNumber + 1));
						voicePromptsAppendString(buffer);
					}
				}

				if (displayedPredictionPass->valid == PREDICTION_RESULT_OK)
				{
					time_t_custom AOS =  displayedPredictionPass->satelliteAOS + ((nonVolatileSettings.timezone & 0x80) ? ((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60) : 0);

					struct tm passDateTime;
					gmtime_r_Custom(&AOS, &passDateTime);
					char buffer[SCREEN_LINE_BUFFER_SIZE];

					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%02u:%02u:%02u %s", passDateTime.tm_hour, passDateTime.tm_min, passDateTime.tm_sec, ((nonVolatileSettings.timezone & 0x80) ? "" : "UTC"));
					if (classicDraw)
					{
						displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2) + 4, buffer, FONT_SIZE_2, dblHeight);
					}

					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						voicePromptsAppendLanguageString(currentLanguage->time);
						voicePromptsAppendString(buffer);
					}

					snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s:%2d%c %3u:%02us", currentLanguage->elevation, satelliteGetMaximumElevation(currentActiveSatellite, currentActiveSatellite->predictions.selectedPassNumber), 176,
							( displayedPredictionPass->satellitePassDuration / 60), (displayedPredictionPass->satellitePassDuration % 60)) ;
					if (classicDraw)
					{
						displayPrintCenteredDoubleHeight(((DISPLAY_SIZE_Y * 3) / 4), buffer, FONT_SIZE_2, dblHeight);
					}

					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						voicePromptsAppendLanguageString(currentLanguage->maximum);

						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2d%c",  satelliteGetMaximumElevation(currentActiveSatellite, currentActiveSatellite->predictions.selectedPassNumber), 176);
						voicePromptsAppendString(buffer);

						voicePromptsAppendPrompt(PROMPT_DURATION);
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u", ( displayedPredictionPass->satellitePassDuration / 60)) ;
						voicePromptsAppendString(buffer);
						voicePromptsAppendLanguageString(currentLanguage->minutes);

						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u", (displayedPredictionPass->satellitePassDuration % 60)) ;
						voicePromptsAppendString(buffer);
						voicePromptsAppendLanguageString(currentLanguage->seconds);

					}
				}

				if ((displayedPredictionPass->valid == PREDICTION_RESULT_LIMIT) && (currentActiveSatellite->predictions.numPasses == 0))
				{
					if (classicDraw)
					{
						displayPrintCenteredDoubleHeight((DISPLAY_SIZE_Y / 2) + 4 - (dblHeight ? (FONT_SIZE_3_HEIGHT / 2) : 0), currentLanguage->list_empty, FONT_SIZE_3, dblHeight);
					}

					voicePromptsAppendString(currentActiveSatellite->name);
					voicePromptsAppendPrompt(PROMPT_SILENCE);
					voicePromptsAppendLanguageString(currentLanguage->list_empty);
				}

				if (announceVP)
				{
					voicePromptsPlay();
				}

				menuSatelliteScreenNextUpdateTime = 0;// Don't do time based update
#if defined(HAS_EMCOM_COLOUR_UI)
				if (emcom)
				{
					emPrediction(displayedPredictionPass);
				}
				else
#endif
				{
					displayRender();
				}
			}
			break;

			case SATELLITE_SCREEN_ALL_PREDICTIONS_LIST:
				{
					time_t_custom passTime;
					satelliteData_t *foundSat = NULL;
					int foundPassNumber;
					satelliteData_t *seatchingSat;
					time_t_custom AOS;
					uint32_t pass;
					int numSatellitesFound = 0;
					int totalPredictions = 0;
					int foundSatelliteIndex = -1;

					predictionsListNumSatellitePassesDisplayed = 0;
					predictionsListSelectedSatellite = 0;

#if defined(HAS_EMCOM_COLOUR_UI)
					if (emcom)
					{
						emListFrame();
					}
					else
#endif
					{
						displayClearBuf();
					}

					for(int i = 0; i< numSatellitesLoaded; i++)
					{
						satelliteDataNative[i].predictions.listDisplayPassSearchStartIndex = 0;
						totalPredictions += satelliteDataNative[i].predictions.numPasses;
					}

					while((predictionsListNumSatellitePassesDisplayed < NUM_PASSES_TO_DISPLAY_ON_LIST_SCREEN) && (numSatellitesFound < totalPredictions))
					{
						foundPassNumber = -1;
						passTime = 0xFFFFFFFF;// highest possible number
						for(int sat = 0; sat < numSatellitesLoaded; sat++)
						{
							seatchingSat = &satelliteDataNative[sat];

							pass = seatchingSat->predictions.listDisplayPassSearchStartIndex;

							if(seatchingSat->predictions.passes[pass].valid == PREDICTION_RESULT_OK)
							{
								AOS = seatchingSat->predictions.passes[pass].satelliteAOS;
								if (AOS < passTime)
								{
									passTime = AOS;
									foundSat = seatchingSat;
									foundSatelliteIndex = sat;
									foundPassNumber = pass;
								}
							}
						}
						if ((foundPassNumber >= 0) && (foundSat != NULL))
						{
							foundSat->predictions.listDisplayPassSearchStartIndex = (foundPassNumber + 1);// last foundPass must be for the foundSatellite

							if (numSatellitesFound >= currentlyDisplayedListPosition)
							{
								char passTimeBuffer[SCREEN_LINE_BUFFER_SIZE];

								AOS = foundSat->predictions.passes[foundPassNumber].satelliteAOS +  ((nonVolatileSettings.timezone & 0x80) ? ((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60) : 0);
								struct tm passDateTime;
								gmtime_r_Custom(&AOS, &passDateTime);

								snprintf(passTimeBuffer, SCREEN_LINE_BUFFER_SIZE, "%02u:%02u:%02u%s", passDateTime.tm_hour, passDateTime.tm_min, passDateTime.tm_sec, ((nonVolatileSettings.timezone & 0x80) ? "" : "UTC"));

#if defined(HAS_EMCOM_COLOUR_UI)
								if (emcom)
								{
									emListRow(predictionsListNumSatellitePassesDisplayed, foundSat, foundPassNumber, &passDateTime);
								}
								else
#endif
								{
									displayPrintCentered(6 + (20 * predictionsListNumSatellitePassesDisplayed), foundSat->name, FONT_SIZE_2);

									snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s %02d%c", passTimeBuffer, satelliteGetMaximumElevation(foundSat, foundPassNumber), 176);
									displayPrintCentered(6 + (20 * predictionsListNumSatellitePassesDisplayed) + 8, buffer, FONT_SIZE_2);

									if ((foundPassNumber == 0) && foundSat->predictions.isVisible)
									{
										uint32_t yPos = 6 + (20 * predictionsListNumSatellitePassesDisplayed);
										displayThemeApply(THEME_ITEM_FG_SATELLITE_COLOUR, THEME_ITEM_BG);
										displayFillRect((DISPLAY_SIZE_X - (DISPLAY_X_POS_MENU_OFFSET * 2) - 6), yPos, 4, 16, false);
										displayThemeResetToDefault();
									}

									if (predictionsListNumSatellitePassesDisplayed == 0)
									{
										displayDrawRect(2 + DISPLAY_X_POS_MENU_OFFSET, 4, (DISPLAY_SIZE_X - (DISPLAY_X_POS_MENU_OFFSET * 2) - 4), 20, true);
									}
								}

								if (predictionsListNumSatellitePassesDisplayed == 0)
								{

									if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
									{
										voicePromptsInit();
										voicePromptsAppendString(foundSat->name);
										voicePromptsAppendLanguageString(currentLanguage->time);
										voicePromptsAppendString(passTimeBuffer);
										voicePromptsAppendLanguageString(currentLanguage->maximum);
										snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2d%c", satelliteGetMaximumElevation(foundSat, foundPassNumber), 176);
										voicePromptsAppendString(buffer);
										if (announceVP)
										{
											voicePromptsPlay();
										}
									}

									foundSat->predictions.selectedPassNumber = foundPassNumber;
									predictionsListSelectedSatellite = foundSatelliteIndex;
									//selectSatellite(predictionsListSelectedSatellite);
								}

								predictionsListNumSatellitePassesDisplayed++;
							}
							numSatellitesFound++;
						}
					}
#if defined(HAS_EMCOM_COLOUR_UI)
					if (emcom && (predictionsListNumSatellitePassesDisplayed == 0))
					{
						emText(4, (EM_LIST_Y + 14), currentLanguage->list_empty, FONT_SIZE_3, THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
					}
					displayThemeResetToDefault();
#endif
					menuSatelliteScreenNextUpdateTime = 0;// Don't do time based update
					displayRender();
				}
				break;

			default:
				break;
		}

		hasRecalculated = false;
	}
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		hasRecalculated = true; // Force full redraw
		updateScreen(ev, false, false);
		return;
	}

	if (displayMode == SATELLITE_SCREEN_SELECTED_SATELLITE)
	{
		bool needsUpdate = false;

		if (KEYCHECK_SHORTUP(ev->keys, KEY_1))
		{
			currentSatelliteFreqIndex = SATELLITE_VOICE_FREQ;
			needsUpdate = true;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_2))
		{
			currentSatelliteFreqIndex = SATELLITE_APRS_FREQ;
			needsUpdate = true;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_3))
		{
			currentSatelliteFreqIndex = SATELLITE_OTHER_FREQ;
			needsUpdate = true;
		}
#if defined(HAS_SSTV_MENU) && defined(I2S_APRS_RX)
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_9))
		{
			// Fork: SSTV picture receive on this satellite, Doppler retune kept running
			menuSystemPushNewMenu(MENU_SSTV);
			return;
		}
#endif

		if (needsUpdate)
		{
			hasRecalculated = true; // Need to set this otherwise the SATELLITE_SCREEN_SELECTED_SATELLITE does not immediately redraw
			nextCalculationTime = 0U; // Need to re-calculate immediately, as QRG has changed.
			updateScreen(ev, false, true);
			return;
		}
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
		{
			menuSystemPopPreviousMenu();
		}
		else
		{
			displayMode = SATELLITE_SCREEN_ALL_PREDICTIONS_LIST;
			voicePromptsInit();
			updateScreen(ev, false, true);
		}
	}

	if (!hasSatelliteKeps)
	{
		return;
	}

	if (ev->keys.event & KEY_MOD_PRESS)
	{
		// Cancels currently beeping alarm
		if ((uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_SATELLITE) &&
				(uiDataGlobal.dateTimeSecs >= uiDataGlobal.SatelliteAndAlarmData.alarmTime))
		{
			uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_NONE;
		}
	}

	if (ev->keys.event & KEY_MOD_PRESS)
	{
		switch(ev->keys.key)
		{
			case KEY_GREEN:
			{
				uint32_t alarmTime;

				// Cancels ongoing alarm
				if ((uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_SATELLITE) &&
						(uiDataGlobal.dateTimeSecs < uiDataGlobal.SatelliteAndAlarmData.alarmTime))
				{
					uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_NONE;
					nextKeyBeepMelody = (int16_t *)MELODY_QUICKKEYS_CLEAR_ACK_BEEP;
					return;
				}

				if (displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
				{
					selectSatellite(predictionsListSelectedSatellite);

					alarmTime = currentActiveSatellite->predictions.passes[currentActiveSatellite->predictions.selectedPassNumber].satelliteAOS - ALARM_OFFSET_SECS;

					if (!(BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (currentActiveSatellite->predictions.isVisible || uiDataGlobal.dateTimeSecs >= alarmTime)))
					{
						displayMode = SATELLITE_SCREEN_SELECTED_SATELLITE_POLAR;//SATELLITE_SCREEN_SELECTED_SATELLITE;
						updateScreen(ev, true, true);
					}
				}

				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					alarmTime = currentActiveSatellite->predictions.passes[currentActiveSatellite->predictions.selectedPassNumber].satelliteAOS - ALARM_OFFSET_SECS;

					if (!(currentActiveSatellite->predictions.isVisible || uiDataGlobal.dateTimeSecs >= alarmTime))
					{
						uiDataGlobal.SatelliteAndAlarmData.alarmTime = currentActiveSatellite->predictions.passes[currentActiveSatellite->predictions.selectedPassNumber].satelliteAOS - ALARM_OFFSET_SECS;
						uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_SATELLITE;
#if defined(PLATFORM_MD9600)
						nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;
#else
						powerOffFinalStage(true, false);
#endif
					}
					else
					{
						nextKeyBeepMelody = (int16_t *)MELODY_ERROR_BEEP;
					}
				}
				break;
			}

			case KEY_RIGHT:
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			case KEY_ROTARY_INCREMENT:
#endif
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (increasePowerLevel(false))
					{
						headerRowIsDirty = true;
					}
					uiNotificationShow(NOTIFICATION_TYPE_POWER, NOTIFICATION_ID_POWER, 1000, NULL, true);
				}
				else
				{
					switch(displayMode)
					{
						case SATELLITE_SCREEN_SELECTED_SATELLITE_PREDICTION:
							if ((currentActiveSatellite->predictions.numPasses > 0) &&
									(currentActiveSatellite->predictions.selectedPassNumber < (currentActiveSatellite->predictions.numPasses - 1)))
							{
								currentActiveSatellite->predictions.selectedPassNumber++;
								updateScreen(ev, true, true);
							}
							break;
						default:
							// more squelch
							if(currentChannelData->sql == 0) //If we were using default squelch level
							{
								currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
							}

							if (currentChannelData->sql < CODEPLUG_MAX_VARIABLE_SQUELCH)
							{
								currentChannelData->sql++;
							}

							announceItem(PROMPT_SQUENCE_SQUELCH, PROMPT_THRESHOLD_3);

							uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, true);
						break;
					}
				}
				break;

			case KEY_LEFT:
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			case KEY_ROTARY_DECREMENT:
#endif
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (decreasePowerLevel())
					{
						headerRowIsDirty = true;
					}
					uiNotificationShow(NOTIFICATION_TYPE_POWER, NOTIFICATION_ID_POWER, 1000, NULL, true);
				}
				else
				{
					switch(displayMode)
					{
					case SATELLITE_SCREEN_SELECTED_SATELLITE_PREDICTION:
						if (currentActiveSatellite->predictions.selectedPassNumber > 0)
						{
							currentActiveSatellite->predictions.selectedPassNumber--;
							updateScreen(ev, false, true);
						}
						break;
					default:
						// less squelch
						if(currentChannelData->sql == 0) //If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
						}

						if (currentChannelData->sql > CODEPLUG_MIN_VARIABLE_SQUELCH)
						{
							currentChannelData->sql--;
						}

						announceItem(PROMPT_SQUENCE_SQUELCH,PROMPT_THRESHOLD_3);

						uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, true);
						break;
					}
				}
				break;

			case KEY_UP:
				if (displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
				{
					if (currentlyDisplayedListPosition > 0)
					{
						currentlyDisplayedListPosition--;
						updateScreen(ev, true, true);
					}
				}
				else
				{
					if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						displayMode--;
						if (displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
						{
							displayMode = NUM_SATELLITE_SCREEN_ITEMS - 1;
						}
						voicePromptsInit();
					}
					else
					{
						uint32_t newSatIndex = uiDataGlobal.SatelliteAndAlarmData.currentSatellite - 1;
						newSatIndex %= numSatellitesLoaded;
						selectSatellite(newSatIndex);
					}

					nextCalculationTime = (ev->time - 1);// force recalculation
					updateScreen(ev, false, true);
				}
				break;

			case KEY_DOWN:
				if (displayMode ==SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
				{
					if (predictionsListNumSatellitePassesDisplayed > 1)
					{
						currentlyDisplayedListPosition++;
						updateScreen(ev, true, true);
					}
				}
				else
				{
					if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						displayMode++;
						if (displayMode == NUM_SATELLITE_SCREEN_ITEMS)
						{
							displayMode = SATELLITE_SCREEN_SELECTED_SATELLITE;
						}
						voicePromptsInit();
					}
					else
					{
						uint32_t newSatIndex = uiDataGlobal.SatelliteAndAlarmData.currentSatellite + 1;
						newSatIndex %= numSatellitesLoaded;
						selectSatellite(newSatIndex);
					}
					nextCalculationTime = (ev->time - 1);// force recalculation
					updateScreen(ev, false, true);
				}

				break;

			case KEY_STAR:
				// Fork: '*' on the list opens the location page (manual position / GPS on), RED comes back
				if ((displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
				{
					menuSystemPushNewMenu(MENU_RADIO_INFOS);
					ev->hasEvent = true;
					ev->function = QUICKKEY_MENUVALUE(MENU_RADIO_INFOS, RADIO_INFOS_LOCATION, 0);
					ev->events = FUNCTION_EVENT;
					menuSystemCallCurrentMenuTick(ev);
					return;
				}
				break;

			case KEY_HASH:
			{
				satellitePass_t *pass;
				uint32_t newSatIndex = uiDataGlobal.SatelliteAndAlarmData.currentSatellite;

				for(int i = 0; i < (numSatellitesLoaded - 1); i++)
				{
					newSatIndex++;
					newSatIndex %= numSatellitesLoaded;

					pass = &satelliteDataNative[newSatIndex].predictions.passes[0];
					if ((pass->valid == PREDICTION_RESULT_OK) &&
						(pass->satelliteAOS  <= uiDataGlobal.dateTimeSecs) &&
						(pass->satelliteLOS  >= uiDataGlobal.dateTimeSecs))
					{
						selectSatellite(newSatIndex);
						updateScreen(ev, true, true);
						break;
					}
				}
			}
				break;
				/*
				 * Testing only. Move time forward 30 minutes
			case KEY_STAR:
				uiDataGlobal.dateTimeSecs += 1800;
				break;
				*/
		}
	}
#if ! (defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380))
	// set as KEY_RIGHT on some platforms (but longdown is not possible on MD380/MD-UV380)
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_INCREASE)
			&& BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		// Long press allows the 5W+ power setting to be selected immediately
		if (increasePowerLevel(true))
		{
			headerRowIsDirty = true;
		}
	}
#endif

	if (ev->events & FUNCTION_EVENT)
	{
		if (QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU)
		{
			displayMode = QUICKKEY_ENTRYID(ev->function);

			updateScreen(ev, true, true);
			return;
		}
		else if (ev->function == FUNC_REDRAW)
		{
			updateScreen(ev, false, false);
		}
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			if (voicePromptsIsPlaying())
			{
				voicePromptsTerminate();
			}
			else
			{
				if ((displayMode == SATELLITE_SCREEN_SELECTED_SATELLITE) || (displayMode ==  SATELLITE_SCREEN_SELECTED_SATELLITE_POLAR))
				{
					voicePromptsInit();
					updateScreen(ev, true, true);
					return;
				}
				else
				{
					voicePromptsPlay();
				}
			}
		}
	}

	if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), displayMode, 0);
	}
}

static bool findSelectedPass = false;
static uint32_t selectedPassAOS = 0;
static bool calculatePredictionsForSatelliteIndex(int satelliteIndex)
{
	satelliteResults_t results;
	satelliteData_t * satellite = &satelliteDataNative[satelliteIndex];

	if ((uiDataGlobal.dateTimeSecs >= satellite->predictions.passes[0].satelliteAOS) && (uiDataGlobal.dateTimeSecs <= satellite->predictions.passes[0].satelliteLOS))
	{
		if ((displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST) && (satellite->predictions.isVisible == false))
		{
			menuSatelliteScreenNextUpdateTime = 1U;
		}
		satellite->predictions.isVisible = true;
	}
	else
	{
		if ((displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST) && satellite->predictions.isVisible)
		{
			menuSatelliteScreenNextUpdateTime = 1U;
		}
		satellite->predictions.isVisible = false;
	}

	// Force rebuilding of the prediction if the the time is after the LOS of the first predicted pass
	if ((satellite->predictions.numPasses > 0) && (uiDataGlobal.dateTimeSecs > satellite->predictions.passes[0].satelliteLOS) && !satellite->predictions.isPredicting)
	{
		findSelectedPass = false;
		if (satellite->predictions.selectedPassNumber != 0)
		{
			selectedPassAOS = satellite->predictions.passes[satellite->predictions.selectedPassNumber].satelliteAOS;
			if (selectedPassAOS >= uiDataGlobal.dateTimeSecs)
			{
				findSelectedPass = true;
			}
		}

		// find passes which are in the past
		int passNum;
		for(passNum=0; passNum<satellite->predictions.numPasses; passNum++)
		{
			if (!(uiDataGlobal.dateTimeSecs > satellite->predictions.passes[passNum].satelliteLOS))
			{
				break;
			}
		}
		int numPassesToMove = satellite->predictions.numPasses - passNum;
		memcpy(&satellite->predictions.passes[0], &satellite->predictions.passes[passNum],  numPassesToMove * sizeof(satellitePass_t));

		int numPassesToClear = NUM_SATELLITE_PREDICTIONS - numPassesToMove;
		memset(&satellite->predictions.passes[numPassesToMove], 0x00, (numPassesToClear) * sizeof(satellitePass_t));// clear all predictions for this satellite

		satellite->predictions.numPasses = numPassesToMove;
		satellite->predictions.numPassBeingPredicted = numPassesToMove;
		satellite->predictions.listDisplayPassSearchStartIndex = 0;
		satellite->predictions.selectedPassNumber = 0;
		satellite->predictions.isPredicting = true;
		satellite->predictions.isVisible = false;


		//memset(&satellite->predictions, 0x00, sizeof(satellitePredictions_t));// clear all predictions for this satellite
		//satellite->predictions.numPassBeingPredicted = 0;
		satellite->predictions.isPredicting = true;
		numTotalSatellitesPredicted--;
		currentlyDisplayedListPosition = 0;// Reset the list position, as there may be less satellites after the passes for this satellite are re-calculated
	}

	if (satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid == PREDICTION_RESULT_NONE)
	{
		time_t_custom predictionStartTime;

		if ((satellite->predictions.numPassBeingPredicted == 0) && (currentPrediction.state == PREDICTION_STATE_NONE))
		{
			satelliteCalculateForDateTimeSecs(satellite, uiDataGlobal.dateTimeSecs, &results, SATELLITE_PREDICTION_LEVEL_TIME_AND_ELEVATION_ONLY);

			if (results.elevation < 0)
			{
				predictionStartTime = uiDataGlobal.dateTimeSecs;
			}
			else
			{
				predictionStartTime = uiDataGlobal.dateTimeSecs - (30 * 60);// If satellite is currently visible. Change prediction start back  30 mins
			}
		}
		else
		{
			predictionStartTime = satellite->predictions.passes[satellite->predictions.numPassBeingPredicted - 1].satelliteAOS +
				satellite->predictions.passes[satellite->predictions.numPassBeingPredicted - 1].satellitePassDuration + 30 * 60;// 30 minutes after the last pass
		}

		if ((currentPrediction.state == PREDICTION_STATE_NONE))
		{
			currentPrediction.state = PREDICTION_STATE_INIT_AOS;// Start the prediction
		}

		if (!satellitePredictNextPassFromDateTimeSecs(&currentPrediction, satellite, predictionStartTime, (uiDataGlobal.dateTimeSecs + (24 * 60 * 60)), 500,  &satellite->predictions.passes[satellite->predictions.numPassBeingPredicted]))
		{
			satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid = PREDICTION_RESULT_NONE;
		}

		switch(currentPrediction.state)
		{
			case PREDICTION_STATE_COMPLETE:
				if (satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].satelliteAOS != 0)
				{

					satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid = PREDICTION_RESULT_OK;
					satellite->predictions.numPasses++;
					if (satellite->predictions.numPasses < (NUM_SATELLITE_PREDICTIONS - 1))
					{
						if (findSelectedPass && (selectedPassAOS == satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].satelliteAOS))
						{
							satellite->predictions.selectedPassNumber = satellite->predictions.numPassBeingPredicted;
							findSelectedPass = false;
						}
						satellite->predictions.numPassBeingPredicted++;
					}
					else
					{
						satellite->predictions.numPasses--;// hack
						satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid = PREDICTION_RESULT_LIMIT;
						currentPrediction.state = PREDICTION_STATE_NONE;// Start the prediction
						satellite->predictions.isPredicting = false;
					}
				}
				currentPrediction.state = PREDICTION_STATE_NONE;// Start the prediction
				break;

			case PREDICTION_STATE_ITERATION_LIMIT:
				// Do something. There has been a problem while computing the predictions
			case PREDICTION_STATE_LIMIT:
				satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid = PREDICTION_RESULT_LIMIT;
				currentPrediction.state = PREDICTION_STATE_NONE;// Start the prediction
				satellite->predictions.isPredicting = false;
				//clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
				findSelectedPass = false;
				return true;
				break;
			case PREDICTION_STATE_NONE:
			case PREDICTION_STATE_INIT_AOS:
			case PREDICTION_STATE_FIND_AOS:
			case PREDICTION_STATE_INIT_LOS:
			case PREDICTION_STATE_FIND_LOS:
				satellite->predictions.passes[satellite->predictions.numPassBeingPredicted].valid = PREDICTION_RESULT_NONE;// move on to next pass
				return false;
				break;
		}
	}
	return true;
}

static void loadKeps(void)
{
	codeplugSatelliteCuctsomDataUnion_t codeplugKepsData;

	//volatile int s = sizeof(codeplugSatelliteData_t);

	hasSatelliteKeps = codeplugGetOpenGD77CustomData(CODEPLUG_CUSTOM_DATA_TYPE_SATELLITE_TLE, (uint8_t *)&codeplugKepsData.data);
	if (hasSatelliteKeps)
	{
		for(numSatellitesLoaded = 0; numSatellitesLoaded < NUM_SATELLITES; numSatellitesLoaded++)
		{
			if (codeplugKepsData.data[numSatellitesLoaded].TLE_Name[0] != 0)
			{
				satelliteTLE2Native(
						codeplugKepsData.data[numSatellitesLoaded].TLE_Name,
						codeplugKepsData.data[numSatellitesLoaded].TLE_Line1,
						codeplugKepsData.data[numSatellitesLoaded].TLE_Line2, &satelliteDataNative[numSatellitesLoaded]) ;

						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_VOICE_FREQ].rxFreq = codeplugKepsData.data[numSatellitesLoaded].rxFreq1;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_VOICE_FREQ].txFreq = codeplugKepsData.data[numSatellitesLoaded].txFreq1;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_VOICE_FREQ].txCTCSS = codeplugKepsData.data[numSatellitesLoaded].txCTCSS1;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_VOICE_FREQ].armCTCSS = codeplugKepsData.data[numSatellitesLoaded].armCTCSS1;

						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_APRS_FREQ].rxFreq = codeplugKepsData.data[numSatellitesLoaded].rxFreq2;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_APRS_FREQ].txFreq = codeplugKepsData.data[numSatellitesLoaded].txFreq2;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_APRS_FREQ].txCTCSS = 0;//codeplugKepsData.data[numSatellitesLoaded].txCTCSS1;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_APRS_FREQ].armCTCSS = 0;//codeplugKepsData.data[numSatellitesLoaded].armCTCSS1;

						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_OTHER_FREQ].rxFreq = codeplugKepsData.data[numSatellitesLoaded].rxFreq3;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_OTHER_FREQ].txFreq = codeplugKepsData.data[numSatellitesLoaded].txFreq3;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_OTHER_FREQ].txCTCSS = 0;//codeplugKepsData.data[numSatellitesLoaded].txCTCSS1;
						satelliteDataNative[numSatellitesLoaded].freqs[SATELLITE_OTHER_FREQ].armCTCSS = 0;//codeplugKepsData.data[numSatellitesLoaded].armCTCSS1;
						memcpy(satelliteDataNative[numSatellitesLoaded].AdditionalData, codeplugKepsData.data[numSatellitesLoaded].AdditionalData, ADDITION_DATA_SIZE);

						memset(&satelliteDataNative[numSatellitesLoaded].predictions, 0x00, sizeof(satellitePredictions_t));
			}
			else
			{
				break;
			}
		}
	}
}

void menuSatelliteScreenClearPredictions(bool reloadKeps)
{
	numTotalSatellitesPredicted = 0;
	currentlyDisplayedListPosition = 0; //reset the list display position if the predictions have been cleared.

	for(int s = 0; s < NUM_SATELLITES; s++)
	{
		memset(&satelliteDataNative[s].predictions, 0x00, sizeof(satellitePredictions_t));
	}

	if (reloadKeps)
	{
		loadKeps();
	}

	currentActiveSatellite = &satelliteDataNative[0];

	uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_NONE;
}

static int menuSatelliteFindNextSatellite(void)
{
	uint32_t AOS;
	int foundSatelliteIndex =-1;
	uint32_t passTime = 0xFFFFFFFF;// highest possible number
	satelliteData_t *seatchingSat;

	for(int sat = 0; sat < numSatellitesLoaded; sat++)
	{
		seatchingSat = &satelliteDataNative[sat];

		if(seatchingSat->predictions.passes[0].valid == PREDICTION_RESULT_OK)
		{
			AOS = seatchingSat->predictions.passes[0].satelliteAOS;
			if (AOS < passTime)
			{
				passTime = AOS;
				foundSatelliteIndex = sat;
			}
		}
	}
	return foundSatelliteIndex;
}

static void selectSatellite(uint32_t selectedSatellite)
{
		uiDataGlobal.SatelliteAndAlarmData.currentSatellite = selectedSatellite;
		currentActiveSatellite = &satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite];
		satelliteVisible = false;// presume initially the new satellite has not been acquired

		calculateActiveSatelliteData(true);

		currentChannelData->txTone = currentActiveSatellite->freqs[currentSatelliteFreqIndex].txCTCSS;

		if (currentChannelData->chMode == RADIO_MODE_NONE)
		{
			currentChannelData->chMode = RADIO_MODE_ANALOG;
			trxSetModeAndBandwidth(currentChannelData->chMode, true);
		}
		hasSelectedSatellite = true;

		aprsBeaconingPrepareSatelliteConfig();

		voicePromptsInit();
}

bool menuSatelliteIsDisplayingHeader(void)
{
#if defined(HAS_EMCOM_COLOUR_UI)
	if (emcomStyleEnabled())
	{
		return false; // the EMCOM view has a title bar, not the radio header
	}
#endif
	return (displayMode == SATELLITE_SCREEN_SELECTED_SATELLITE);
}

void menuSatelliteTxScreen(uint32_t txTimeSecs)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	displayClearBuf();
	uiUtilityRenderHeader(false, false, false);

	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, " %d ", txTimeSecs);
	uiUtilityDisplayInformation(buffer, DISPLAY_INFO_TX_TIMER, -1);

	satelliteCalculateForDateTimeSecs(currentActiveSatellite, uiDataGlobal.dateTimeSecs, &currentSatelliteResults, SATELLITE_PREDICTION_LEVEL_FULL);

	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s:%3d%c %s:%3d%c", currentLanguage->azimuth, currentSatelliteResults.azimuthAsInteger, 176, currentLanguage->elevation, currentSatelliteResults.elevationAsInteger, 176);
	displayPrintCentered(DISPLAY_Y_POS_RX_FREQ + 1, buffer, FONT_SIZE_3);

	int val_before_dp = currentChannelData->txFreq / 100000;
	int val_after_dp = currentChannelData->txFreq - val_before_dp * 100000;
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE,  "%d.%05d MHz", val_before_dp, val_after_dp);
	displayPrintCentered(DISPLAY_Y_POS_TX_FREQ, buffer, FONT_SIZE_3);

	displayRender();
}

void menuSatelliteSetFullReload(void)
{
	displayMode = SATELLITE_SCREEN_ALL_PREDICTIONS_LIST;
	hasSelectedSatellite = false;
	hasSatelliteKeps = false;
}

bool menuSatelliteDopplerAvailable(void)
{
	return (hasSatelliteKeps && hasSelectedSatellite && (currentActiveSatellite != NULL) && (currentChannelData == &satelliteChannelData));
}

void menuSatelliteDopplerTick(void)
{
	if (menuSatelliteDopplerAvailable())
	{
		calculateActiveSatelliteData(false);
	}
}

// Fork: the SSTV presets borrow the channel data (no spare RAM for a copy of their own). The
// satellite is selected again (full set up of the channel data) the next time this screen opens.
CodeplugChannel_t *menuSatelliteBorrowChannelData(void)
{
	hasSelectedSatellite = false;
	return &satelliteChannelData;
}

const char *menuSatelliteActiveName(void)
{
	return ((currentActiveSatellite != NULL) ? currentActiveSatellite->name : "");
}

static void exitCallback(void *data)
{
	if (displayMode == SATELLITE_SCREEN_ALL_PREDICTIONS_LIST)
	{
		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
	}

	if (menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE)
	{
		currentChannelData = &channelScreenChannelData;
		uiChannelModeLoadChannelData(true, false);
	}
	else
	{
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
		uiVFOModeLoadChannelData(false);
	}

	aprsBeaconingResetTimers();
	displayMode = SATELLITE_SCREEN_ALL_PREDICTIONS_LIST;
}

static void calculateActiveSatelliteData(bool forceFrequencyUpdate)
{
	gmtime_r_Custom(&uiDataGlobal.dateTimeSecs, &timeAndDate);

	satelliteCalculateForDateTimeSecs(currentActiveSatellite, uiDataGlobal.dateTimeSecs, &currentSatelliteResults, SATELLITE_PREDICTION_LEVEL_FULL);

	uint32_t rxF = currentSatelliteResults.freqs[currentSatelliteFreqIndex].rxFreq / 10;
	uint32_t txF = currentSatelliteResults.freqs[currentSatelliteFreqIndex].txFreq / 10;
	currentChannelData->txFreq = txF;// Tx freq can be updated immediately
	// Only update the Rx freq is its off by more than 500Hz,
	// to prevent constant dropout in the Rx caused by the process of changing the frequency of the AT1846S
	// Note value is stored in the channel data a 10Hz resolution hence the / 10
	if (forceFrequencyUpdate || (abs(currentChannelData->rxFreq - rxF) > (500 / 10)))
	{
		currentChannelData->rxFreq = rxF;

		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);

		trxPostponeReadRSSIAndNoise(100);
	}

	// Handle VP for AOS and LOS
	if (currentSatelliteResults.elevation > 0)
	{
		if (!satelliteVisible)
		{
			satelliteVisible = true;// satellite acquired

			voicePromptsInitWithOverride();
			voicePromptsAppendLanguageString(currentLanguage->satellite);
			voicePromptsAppendString(currentActiveSatellite->name);
			voicePromptsAppendLanguageString(currentLanguage->azimuth);

			char buf[16];
			snprintf(buf, 16, "%03d%c", currentSatelliteResults.azimuthAsInteger, 176);
			voicePromptsAppendString(buf);
			voicePromptsPlay();
		}
	}
	else
	{
		if (satelliteVisible)
		{
			satelliteVisible = false;// satellite lost
			voicePromptsInitWithOverride();
			voicePromptsAppendLanguageString(currentLanguage->satellite);
			voicePromptsAppendLanguageString(currentLanguage->off);
			voicePromptsPlay();
		}
	}
	hasRecalculated = true;
}
