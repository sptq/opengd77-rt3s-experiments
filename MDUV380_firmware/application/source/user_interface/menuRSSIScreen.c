/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
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
#include "user_interface/uiGlobals.h"
#include "functions/calibration.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "hardware/radioHardwareInterface.h"
#include "functions/sound.h"
#include "interfaces/i2s.h"
#include "functions/aprsRx.h"
#include "functions/toneRxDecoders.h"
#include "functions/ticks.h"
#include "user_interface/uiEmcom.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

// Fox-hunt mode (fork): '#' toggles it on the RSSI screen.
//  - periodic beep whose pitch follows the signal strength,
//  - max-hold of the dBm value, with a marker on the S-meter,
//  - the GPS position of that maximum, shown as distance/bearing from here,
//  - '0' resets the max-hold.
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
#define FOX_HUNT_SUPPORTED
#endif


#if defined(PLATFORM_MD2017)
#define SECONDARY_DISPLAY_OFFSET  50
#endif


static menuStatus_t menuRSSIExitCode = MENU_STATUS_SUCCESS;
//static calibrationRSSIMeter_t rssiCalibration; // UNUSED
static void updateScreen(bool forceRedraw, bool firstRun);
static void handleEvent(uiEvent_t *ev);
static void updateVoicePrompts(bool flushIt, bool spellIt);

static int dBm[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawSignal[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawNoise[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static bool displayRawValues = false;

#if defined(FOX_HUNT_SUPPORTED)
#define FOX_BEEP_INTERVAL_MS     600
#define FOX_BEEP_DURATION_MS      50
#define FOX_DBM_FLOOR           -130
#define FOX_DBM_CEIL             -40
#define FOX_FREQ_MIN             300
#define FOX_FREQ_MAX            2400
#define FOX_INFO_Y_POS          (DISPLAY_Y_POS_RSSI_BAR + 26) // Just below the S-meter scale labels
#define FOX_INFO_LINE2_Y_POS    (FOX_INFO_Y_POS + FONT_SIZE_3_HEIGHT + 1)
_Static_assert((FOX_INFO_LINE2_Y_POS + FONT_SIZE_1_HEIGHT) <= DISPLAY_SIZE_Y, "Fox-hunt info does not fit on screen");

static bool foxHuntEnabled = false;
static int foxMaxDbm = INT16_MIN;
static bool foxMaxHasPosition = false;
static double foxMaxLat = 0.0;
static double foxMaxLon = 0.0;
static uint32_t foxLastBeepTime = 0;
static int16_t foxBeepMelody[] = { 0, FOX_BEEP_DURATION_MS, -1, -1 };

static void foxHuntReset(void);
static void foxHuntUpdate(uint32_t now);
static void foxHuntDrawInfo(void);

#endif

static const int barX = 9;
DECLARE_SMETER_ARRAY(rssiMeterBar, (DISPLAY_SIZE_X - (barX - 1)));

menuStatus_t menuRSSIScreen(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		//calibrationGetRSSIMeterParams(&rssiCalibration); // UNUSED
		menuDataGlobal.numItems = 0;
#if defined(FOX_HUNT_SUPPORTED)
		foxHuntReset();
#endif
		displayClearBuf();
		menuDisplayTitle(currentLanguage->rssi);
		displayRenderRows(0, 2);

		updateScreen(true, true);
	}
	else
	{
		menuRSSIExitCode = MENU_STATUS_SUCCESS;
		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
		{
			m = ev->time;
			updateScreen(false, false);
		}

#if defined(FOX_HUNT_SUPPORTED)
		if (foxHuntEnabled)
		{
			foxHuntUpdate(ev->time);
		}
#endif
	}

	return menuRSSIExitCode;
}

// Returns S-Unit 0..9..10(S9+10dB)..15(S9+60)
static int32_t getSignalStrength(int dbm)
{
	for (int8_t i = 15; i >= 0; i--)
	{
		if (dbm >= DBM_LEVELS[i])
		{
			return i;
		}
	}

	return 0;
}

static void drawMeterGraticule(int16_t vOffset)
{
	// Draw S-Meter outer frame
	displayDrawRect((barX - 2), (vOffset - 2), (DISPLAY_SIZE_X - (barX - 2)), (8 + 4), true);
	// Clear the right V line of the frame
	displayDrawFastVLine((DISPLAY_SIZE_X - 1), (vOffset - 1), (8 + 2), false);
	// S9+xx H Dots
	for (int16_t i = ((barX - 2) + (rssiMeterBar[9] * 2) + 1); i < DISPLAY_SIZE_X; i += 4)
	{
		displayDrawFastHLine(i, (vOffset - 2), 2, false);
	}
	// +10..60dB
	displayFillRect(((barX - 2) + (rssiMeterBar[9] * 2) + 2), (vOffset + 8) + 2,
			(DISPLAY_SIZE_X - ((barX - 2) + (rssiMeterBar[9] * 2) + 2)), 2, false);

	// Draw S, Numbers and ticks
	displayPrintAt(1, vOffset, "S", FONT_SIZE_1_BOLD);

	int xPos;
	int currentMode = trxGetMode();

	for (uint8_t i = 0; i < 10; i++)
	{
		// Scale the bar graph so values S0 - S9 take 70% of the scale width, and signals above S9 take the last 30%
		// On DMR the max signal is S9+10, so teh entire bar can be the sale scale
		// ON FM signals above S9, the scale is compressed to 2/STRONG_SIGNAL_RESCALE
		if ((i <= 9) || (currentMode == RADIO_MODE_DIGITAL))
		{
			xPos = rssiMeterBar[i];
		}
		else
		{
			xPos = ((rssiMeterBar[i] - rssiMeterBar[9]) / STRONG_SIGNAL_RESCALE) + rssiMeterBar[9];
		}
		xPos *= 2;

		// V ticks
		displayDrawFastVLine(((barX - 2) + xPos), (vOffset + 8) + 2, ((i % 2) ? 3 : 1), ((i < 10) ? true : false));

		if ((i % 2) && (i < 10))
		{
			char buf[2];

			sprintf(buf, "%d", i);
			displayPrintAt(((((barX - 2) + xPos) - 2) - 1)/* FONT_2 H offset */, vOffset + 15
#if defined(PLATFORM_RD5R)
					-1
#endif
					, buf, FONT_SIZE_2);
		}
	}
}

#if defined(HAS_EMCOM_UI)
#if defined(FOX_HUNT_SUPPORTED)
static bool foxHuntGetPosition(double *lat, double *lon);
#endif

// Fork: EMCOM style RSSI screen: big readout, 32-segment meter with an S scale, fox-hunt panel, key bar
#define EM_METER_Y   58
#define EM_METER_H   12

static int emcomSegments(int dbm)
{
	if (dbm <= SMETER_S0)
	{
		return 0;
	}
	if (dbm <= SMETER_S9)
	{
		return (((dbm - SMETER_S0) * 20) / (SMETER_S9 - SMETER_S0));
	}
	return CLAMP((20 + ((dbm - SMETER_S9) / 5)), 0, 32);
}

static void emcomRssiScreen(void)
{
	char buf[32];
	int dbm = dBm[RADIO_DEVICE_PRIMARY];
	int lit = emcomSegments(dbm);
	uint32_t f = currentChannelData->rxFreq;

	displayClearBuf();
	menuDisplayTitle(currentLanguage->rssi);

	// frequency / mode strip
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, 11, false);
	snprintf(buf, sizeof(buf), "%u.%04u MHz %s", (unsigned)(f / 100000U), (unsigned)((f % 100000U) / 10U),
			((trxGetMode() == RADIO_MODE_DIGITAL) ? "DMR" : (trxGetBandwidthIs25kHz() ? "FM" : "FMN")));
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (MENU_HEADER_HEIGHT + 2), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
#if defined(FOX_HUNT_SUPPORTED)
	if (foxHuntEnabled)
	{
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		displayFillRect((DISPLAY_SIZE_X - 24), (MENU_HEADER_HEIGHT + 1), 22, 9, false);
		displayPrintCore((DISPLAY_SIZE_X - 22), (MENU_HEADER_HEIGHT + 2), "FOX", FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	}
#endif

	// readout: dBm (16x32) and S units (8x16)
	snprintf(buf, sizeof(buf), "%d", dbm);
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintCore(2, 24, buf, FONT_SIZE_4, TEXT_ALIGN_LEFT, false);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCore((int16_t)(4 + (strlen(buf) * 16)), 42, "dBm", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	if (dbm >= SMETER_S9)
	{
		int over = (((dbm - SMETER_S9) / 10) * 10);
		snprintf(buf, sizeof(buf), ((over > 0) ? "S9+%d" : "S9"), over);
	}
	else
	{
		snprintf(buf, sizeof(buf), "S%d", CLAMP(((dbm - SMETER_S0) / 4), 0, 9));
	}
	displayThemeApply(((dbm > SMETER_S9) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_RSSI_BAR), THEME_ITEM_BG);
	displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 8)), 32, buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, false);

	// meter
	for (int i = 0; i < 32; i++)
	{
		displayThemeApply(((i < lit) ? ((i < 20) ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_RSSI_BAR_S9P) : THEME_ITEM_BG_HEADER_TEXT), THEME_ITEM_BG);
		displayFillRect((int16_t)(i * 5), EM_METER_Y, 4, EM_METER_H, false);
	}
#if defined(FOX_HUNT_SUPPORTED)
	if (foxHuntEnabled && (foxMaxDbm != INT16_MIN))
	{
		int16_t mx = (int16_t)((emcomSegments(foxMaxDbm) * 5) - 1);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayFillRect(((mx < 0) ? 0 : mx), (EM_METER_Y - 3), 2, (EM_METER_H + 6), false);
	}
#endif

	// scale
	{
		static const struct { int seg; const char *label; } marks[] =
		{
			{ 2, "1" }, { 7, "3" }, { 11, "5" }, { 16, "7" }, { 20, "9" }, { 24, "+20" }, { 28, "+40" }
		};

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		for (size_t i = 0; i < (sizeof(marks) / sizeof(marks[0])); i++)
		{
			int16_t x = (int16_t)(marks[i].seg * 5);
			displayDrawFastVLine(x, (EM_METER_Y + EM_METER_H + 1), 2, true);
			displayPrintCore((int16_t)(x - ((strlen(marks[i].label) * 6) / 2) + 1), (EM_METER_Y + EM_METER_H + 4), marks[i].label, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		}
	}

	int16_t y = 90;
	if (displayRawValues)
	{
		snprintf(buf, sizeof(buf), "RAW SIG %u  NOISE %u", rawSignal[RADIO_DEVICE_PRIMARY], rawNoise[RADIO_DEVICE_PRIMARY]);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(3, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		y += 10;
	}

#if defined(FOX_HUNT_SUPPORTED)
	if (foxHuntEnabled)
	{
		double lat, lon;

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(3, y, "MAX", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		snprintf(buf, sizeof(buf), ((foxMaxDbm == INT16_MIN) ? "--" : "%ddBm"), foxMaxDbm);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(24, y, buf, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);

		if (foxMaxHasPosition && foxHuntGetPosition(&lat, &lon))
		{
			int meters = (int)distanceBetweenTwoCoords(lat, lon, foxMaxLat, foxMaxLon, false);
			int bearing = (int)courseTo(lat, lon, foxMaxLat, foxMaxLon);

			if (meters < 10000)
			{
				snprintf(buf, sizeof(buf), "%dm %03d", meters, bearing);
			}
			else
			{
				snprintf(buf, sizeof(buf), "%dkm %03d", (meters / 1000), bearing);
			}
			displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
		}
		else
		{
			snprintf(buf, sizeof(buf), (foxMaxHasPosition ? "no GPS fix" : "no position"));
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		}
		displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 6)), y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
#endif

	// keys
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, (DISPLAY_SIZE_Y - 11), DISPLAY_SIZE_X, 11, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
#if defined(FOX_HUNT_SUPPORTED)
	displayPrintCore(3, (DISPLAY_SIZE_Y - 9), (foxHuntEnabled ? "* RAW  # FOX  0 RST  9 APRS" : "* RAW  # FOX  9 APRS"), FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
#else
	displayPrintCore(3, (DISPLAY_SIZE_Y - 9), "* RAW", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
#endif

	displayThemeResetToDefault();
	displayRender();
}
#endif

static void updateScreen(bool forceRedraw, bool isFirstRun)
{
	char buffer[LOCATION_TEXT_BUFFER_SIZE];
	int barWidth;
	int rssi[RADIO_DEVICE_MAX];
	int16_t yValuePos, yBarPos;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		rssi[device] = dBm[device] = trxGetRSSIdBm(device);
		rawSignal[device] = trxGetSignalRaw(device);
		rawNoise[device] = trxGetNoiseRaw(device);
	}

	if (isFirstRun && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->rssi);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		updateVoicePrompts(false, true);
	}

#if defined(HAS_EMCOM_UI)
	if (emcomStyleEnabled())
	{
		emcomRssiScreen();
		return;
	}
#endif

	if (forceRedraw)
	{
		yBarPos = DISPLAY_Y_POS_RSSI_BAR;

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		// Clear whole drawing region
		displayFillRect(0, 14, DISPLAY_SIZE_X, DISPLAY_SIZE_Y - 14, true);

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			drawMeterGraticule(yBarPos);
#if defined(PLATFORM_MD2017)
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}

		displayThemeResetToDefault();
	}
	else
	{
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE;

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			// Clear dBm region value
			displayFillRect((displayRawValues ? 0 : ((DISPLAY_SIZE_X - (7 * 8)) >> 1)), yValuePos, (displayRawValues ? DISPLAY_SIZE_X : (7 * 8)), FONT_SIZE_3_HEIGHT, true);
#if defined(PLATFORM_MD2017)
			yValuePos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
	}

	yValuePos = DISPLAY_Y_POS_RSSI_VALUE;
	yBarPos = DISPLAY_Y_POS_RSSI_BAR;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		if (displayRawValues)
		{
			snprintf(buffer, LOCATION_TEXT_BUFFER_SIZE, "%d%s [%u %u]", dBm[device], "dBm", rawSignal[device], rawNoise[device]);
		}
		else
		{
			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%s", dBm[device], "dBm");
		}
		displayPrintCentered(yValuePos, buffer, FONT_SIZE_3);

#if 0 // DEBUG
		sprintf(buffer, "%d", currentRadioDevice->trxRxSignal);
		displayFillRect((DISPLAY_SIZE_X - (4 * 8)), yValuePos, (4 * 8), 8, true);
		ucPrintCore((DISPLAY_SIZE_X - ((strlen(buffer) + 1) * 8)), yValuePos, buffer, FONT_SIZE_2, TEXT_ALIGN_RIGHT, false);
#endif

		if ((rssi[device] > SMETER_S9) && (trxGetMode() == RADIO_MODE_ANALOG))
		{
			// In Analog mode, the max RSSI value from the hardware is over S9+60.
			// So scale this to fit in the last 30% of the display
			rssi[device] = ((rssi[device] - SMETER_S9) / STRONG_SIGNAL_RESCALE) + SMETER_S9;
		}
		// Scale the entire bar by 2.
		// Because above S9 the values are scaled to 1/5.
		// This can result in the signal below S9 being doubled in scale (depending on STRONG_SIGNAL_RESCALE)
		// Signals above S9 the scales is compressed to 2/STRONG_SIGNAL_RESCALE.
		rssi[device] = (rssi[device] - SMETER_S0) * 2;

		barWidth = ((rssi[device] * rssiMeterBarNumUnits) / rssiMeterBarDivider);
		barWidth = CLAMP((barWidth - 1), 0, (DISPLAY_SIZE_X - barX));

		if (barWidth)
		{
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
			displayFillRect(barX, yBarPos, barWidth, 8, false);
			displayThemeResetToDefault();
		}

		// Clear the end of the bar area, if needed
		if (barWidth < (DISPLAY_SIZE_X - barX))
		{
			displayFillRect(barX + barWidth, yBarPos, (DISPLAY_SIZE_X - barX) - barWidth, 8, true);
		}

#if defined(HAS_COLOURS)
		if (rssi[device] > SMETER_S9)
		{
			int xPos;

			xPos = (rssiMeterBar[9] * 2);

			if (barWidth > xPos)
			{
				displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
				displayFillRect((barX + xPos), yBarPos, (barWidth - xPos), 8, false);
				displayThemeResetToDefault();
			}
		}
#endif

#if defined(FOX_HUNT_SUPPORTED)
		// Max-hold marker on the S-meter (same scaling as the live bar).
		if (foxHuntEnabled && (device == RADIO_DEVICE_PRIMARY) && (foxMaxDbm != INT16_MIN))
		{
			int maxRssi = foxMaxDbm;

			if ((maxRssi > SMETER_S9) && (trxGetMode() == RADIO_MODE_ANALOG))
			{
				maxRssi = ((maxRssi - SMETER_S9) / STRONG_SIGNAL_RESCALE) + SMETER_S9;
			}
			maxRssi = (maxRssi - SMETER_S0) * 2;

			int maxWidth = CLAMP((((maxRssi * rssiMeterBarNumUnits) / rssiMeterBarDivider) - 1), 0, (DISPLAY_SIZE_X - barX - 2));

			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayFillRect((barX + maxWidth), yBarPos, 2, 8, false);
			displayThemeResetToDefault();
		}
#endif

#if defined(PLATFORM_MD2017)
		yValuePos += SECONDARY_DISPLAY_OFFSET;
		yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
	}

#if defined(FOX_HUNT_SUPPORTED)
	if (foxHuntEnabled)
	{
		foxHuntDrawInfo();
	}
	else if (forceRedraw)
	{
		displayFillRect(0, FOX_INFO_Y_POS, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - FOX_INFO_Y_POS), true);
	}
#endif

	if (forceRedraw || uiNotificationIsVisible())
	{
		displayRender();
	}
	else
	{
		int16_t yStartValuePos = DISPLAY_Y_POS_RSSI_VALUE;
		int16_t yStartBarPos = DISPLAY_Y_POS_RSSI_BAR;

		// Y end positions
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE + FONT_SIZE_3_HEIGHT;
		yBarPos = DISPLAY_Y_POS_RSSI_BAR + 8;

#if defined(PLATFORM_RD5R)
#warning CHECK ME (end pos)
		displayRenderRows((DISPLAY_Y_POS_RSSI_VALUE / 8), (yValuePos / 8) + 1);
#else

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			displayRenderRows((yStartValuePos / 8), (yValuePos / 8) + 1);
			displayRenderRows((yStartBarPos / 8), (yBarPos / 8) + 1);
#if defined(FOX_HUNT_SUPPORTED)
			if (foxHuntEnabled)
			{
				displayRenderRows((FOX_INFO_Y_POS / 8), DISPLAY_NUMBER_OF_ROWS);
			}
#endif

#if defined(PLATFORM_MD2017)
			yStartValuePos += SECONDARY_DISPLAY_OFFSET;
			yStartBarPos += SECONDARY_DISPLAY_OFFSET;
			yValuePos += SECONDARY_DISPLAY_OFFSET;
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
#endif
	}
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(true, false);
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{
		bool wasPlaying = false;

		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (ev->keys.key == 0))
		{
			// Stop playback or update signal strength
			if ((wasPlaying = voicePromptsIsPlaying()) == false)
			{
				updateVoicePrompts(true, false);
			}
		}

		if (repeatVoicePromptOnSK1(ev))
		{
			if (wasPlaying && voicePromptsIsPlaying())
			{
				voicePromptsTerminate();
			}
			return;
		}
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		displayRawValues = !displayRawValues;
		updateScreen(true, false);
	}
#if defined(FOX_HUNT_SUPPORTED)
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		char buf[SCREEN_LINE_BUFFER_SIZE];

		foxHuntEnabled = !foxHuntEnabled;
		foxHuntReset();
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "Fox hunt: %s", (foxHuntEnabled ? currentLanguage->on : currentLanguage->off));
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000U, buf, false);
		updateScreen(true, false);
	}
#if defined(I2S_APRS_RX)
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_9) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
	{
		foxHuntEnabled = false;
		menuSystemPushNewMenu(MENU_APRS_RX); // shortcut to main menu > APRS RX
	}
#endif
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_0) && foxHuntEnabled && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
	{
		foxHuntReset();
		soundSetMelody(MELODY_ACK_BEEP);
		updateScreen(true, false);
	}
#endif
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
	}
}

static void updateVoicePrompts(bool flushIt, bool spellIt)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		uint8_t S = getSignalStrength(dBm[RADIO_DEVICE_PRIMARY]);

		if (flushIt)
		{
			voicePromptsInit();
		}

		voicePromptsAppendPrompt(PROMPT_S);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		if (S > 9)
		{
			voicePromptsAppendPrompt(PROMPT_9);
			voicePromptsAppendPrompt(PROMPT_PLUS);
			voicePromptsAppendInteger(10 * ((S - 10) + 1));
		}
		else
		{
			voicePromptsAppendPrompt(PROMPT_0 + S);
		}

		if (spellIt)
		{
			promptsPlayNotAfterTx();
		}
	}
}

#if defined(FOX_HUNT_SUPPORTED)
static void foxHuntReset(void)
{
	foxMaxDbm = INT16_MIN;
	foxMaxHasPosition = false;
	foxLastBeepTime = 0;
}

static bool foxHuntGetPosition(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) > GPS_MODE_OFF) &&
			((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return true;
	}
#endif
	UNUSED_PARAMETER(lat);
	UNUSED_PARAMETER(lon);
	return false;
}

// Called every UI tick while fox-hunt mode is on: max-hold and the pitch beep.
static void foxHuntUpdate(uint32_t now)
{
	int dbm = dBm[RADIO_DEVICE_PRIMARY];

	if (dbm > foxMaxDbm)
	{
		double lat, lon;

		foxMaxDbm = dbm;
		if (foxHuntGetPosition(&lat, &lon))
		{
			foxMaxLat = lat;
			foxMaxLon = lon;
			foxMaxHasPosition = true;
		}
	}

	if (((now - foxLastBeepTime) >= FOX_BEEP_INTERVAL_MS) && (trxTransmissionEnabled == false) &&
			(soundMelodyIsPlaying() == false) && (voicePromptsIsPlaying() == false))
	{
		int level = CLAMP(dbm, FOX_DBM_FLOOR, FOX_DBM_CEIL) - FOX_DBM_FLOOR;

		foxBeepMelody[0] = FOX_FREQ_MIN + ((level * (FOX_FREQ_MAX - FOX_FREQ_MIN)) / (FOX_DBM_CEIL - FOX_DBM_FLOOR));
		soundSetMelody(foxBeepMelody);
		foxLastBeepTime = now;
	}
}

static void foxHuntDrawInfo(void)
{
	char buf[LOCATION_TEXT_BUFFER_SIZE];

	displayFillRect(0, FOX_INFO_Y_POS, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - FOX_INFO_Y_POS), true);

	if (foxMaxDbm == INT16_MIN)
	{
		snprintf(buf, sizeof(buf), "Max: --");
	}
	else
	{
		snprintf(buf, sizeof(buf), "Max: %ddBm", foxMaxDbm);
	}
	displayPrintCentered(FOX_INFO_Y_POS, buf, FONT_SIZE_3);

	double lat, lon;

	if (foxMaxHasPosition && foxHuntGetPosition(&lat, &lon))
	{
		int meters = (int)distanceBetweenTwoCoords(lat, lon, foxMaxLat, foxMaxLon, false);
		int bearing = (int)courseTo(lat, lon, foxMaxLat, foxMaxLon);

		if (meters < 10000)
		{
			snprintf(buf, sizeof(buf), "Max @ %dm %03d deg", meters, bearing);
		}
		else
		{
			snprintf(buf, sizeof(buf), "Max @ %dkm %03d deg", (meters / 1000), bearing);
		}
	}
	else
	{
		snprintf(buf, sizeof(buf), (foxMaxHasPosition ? "Max @ (no GPS fix)" : "Max @ no position"));
	}
	displayPrintCentered(FOX_INFO_LINE2_Y_POS, buf, FONT_SIZE_1);
}

#endif
