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

#if defined(I2S_APRS_RX)
// Fork: APRS RX monitor. '9' toggles it; it takes the whole screen: status
// line + one of the views, cycled with '#': received frames (UP/DOWN select,
// '*' raw: full TNC2 text + hex dump), stations (distance / bearing / age),
// radar (north up, own position in the centre), APRS messages, DTMF and CW
// decoders (SSTV has its own menu entry, menuSSTV.c). '0' clears the current view. GREEN on a message / station:
// reply / new message (canned texts); '*' on a station: GPS waypoint.
#define APRS_MON_REDRAW_MS      1000U
#define APRS_MON_CHARS          26     // 160 px / 6 px font
#define APRS_MON_STATUS_Y       (MENU_HEADER_HEIGHT + 2)
#define APRS_MON_LIST_Y         (APRS_MON_STATUS_Y + FONT_SIZE_1_HEIGHT + 3)
#define APRS_MON_BLOCK_H        ((3 * FONT_SIZE_1_HEIGHT) + 1)
#define APRS_MON_BLOCKS         ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y) / APRS_MON_BLOCK_H)
#define APRS_MON_RAW_LINES      ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y) / FONT_SIZE_1_HEIGHT)
#define APRS_MON_HEX_PER_LINE   8U
typedef enum
{
	APRS_MON_VIEW_FRAMES = 0,
	APRS_MON_VIEW_STATIONS,
	APRS_MON_VIEW_RADAR,
	APRS_MON_VIEW_MESSAGES,
	APRS_MON_VIEW_DTMF,
	APRS_MON_VIEW_CW,
	APRS_MON_VIEW_MAX
} aprsMonView_t;
#if defined(HAS_GPS) && (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701))
#define APRS_MON_WAYPOINT
#endif
static const char *aprsMonReplies[] = { "QSL", "OK", "73", "Roger", "Thanks!", "Yes", "No", "Call me", "QRV?", "QRX 5 min", "On my way", "QRT" };
#define APRS_MON_NUM_REPLIES    (sizeof(aprsMonReplies) / sizeof(aprsMonReplies[0]))
static uint32_t aprsMonMsgSelected = 0;
static bool aprsMonReplyActive = false;
static uint32_t aprsMonReplyIndex = 0;
static char aprsMonReplyTo[APRS_RX_CALL_LEN];
static uint32_t aprsMonToneSerial = 0;

static bool aprsMonEnabled = false;
static aprsMonView_t aprsMonView = APRS_MON_VIEW_FRAMES;
static char aprsMonStationName[APRS_RX_CALL_LEN]; // selection in the stations / radar views
static uint32_t aprsMonLastGen = 0;
static bool aprsMonRawView = false;
static uint32_t aprsMonSelected = 0;   // 0 = newest
static uint32_t aprsMonRawScroll = 0;
static uint32_t aprsMonLastDraw = 0;
static APRS_RX_CCMRAM char aprsMonText[300]; // TNC2 text of the frame shown in raw view (CCM, always written before use)
static void aprsMonSetEnabled(bool enable);
static void aprsMonUpdate(uint32_t now);
static void aprsMonDraw(void);
static bool aprsMonHandleEvent(uiEvent_t *ev);
#endif
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
#if defined(I2S_APRS_RX)
		aprsMonSetEnabled(false);
#endif
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

#if defined(I2S_APRS_RX)
		if (aprsMonEnabled)
		{
			aprsMonUpdate(ev->time);
		}
		else
#endif
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

#if defined(I2S_APRS_RX)
	if (aprsMonEnabled)
	{
		aprsMonDraw();
		return;
	}
#endif

	if (isFirstRun && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->rssi);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		updateVoicePrompts(false, true);
	}

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

#if defined(I2S_APRS_RX)
	if (aprsMonEnabled && aprsMonHandleEvent(ev))
	{
		return;
	}
#endif

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
#if defined(I2S_APRS_RX)
		aprsMonSetEnabled(false);
#endif
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
#if defined(I2S_APRS_RX)
		aprsMonSetEnabled(false); // fox-hunt beeps need the I2S
#endif
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "Fox hunt: %s", (foxHuntEnabled ? currentLanguage->on : currentLanguage->off));
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000U, buf, false);
		updateScreen(true, false);
	}
#if defined(I2S_APRS_RX)
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_9) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
	{
		foxHuntEnabled = false;
		aprsMonSetEnabled(true);
		updateScreen(true, false);
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
#if defined(I2S_APRS_RX)
static void aprsMonSetEnabled(bool enable)
{
	aprsMonEnabled = enable;
	aprsMonView = APRS_MON_VIEW_FRAMES;
	aprsMonRawView = false;
	aprsMonSelected = 0;
	aprsMonRawScroll = 0;
	aprsMonStationName[0] = 0;
	aprsMonLastDraw = 0;
	aprsMonLastGen = aprsRxGetGeneration();
	aprsMonMsgSelected = 0;
	aprsMonReplyActive = false;
	aprsRxSetMonitor(enable);
}

static void aprsMonUpdate(uint32_t now)
{
	uint32_t gen = aprsRxGetGeneration();

	if (gen != aprsMonLastGen)
	{
		// Keep the same frame selected (and shown in raw view) when new ones arrive
		if ((aprsMonSelected != 0) || aprsMonRawView)
		{
			aprsMonSelected += (gen - aprsMonLastGen);
		}
		if (aprsMonSelected >= aprsRxHistoryCount())
		{
			aprsMonSelected = ((aprsRxHistoryCount() > 0) ? (aprsRxHistoryCount() - 1) : 0);
		}
		aprsMonLastGen = gen;
		aprsMonLastDraw = 0; // redraw now
	}

	if ((aprsMonView == APRS_MON_VIEW_DTMF) || (aprsMonView == APRS_MON_VIEW_CW))
	{
		char tmp[2];
		uint32_t serial;

		aprsRxToneText((aprsMonView == APRS_MON_VIEW_CW), tmp, sizeof(tmp), &serial);
		if (serial != aprsMonToneSerial)
		{
			aprsMonToneSerial = serial;
			aprsMonLastDraw = 0;
		}
	}

	if ((aprsMonLastDraw == 0) || ((now - aprsMonLastDraw) >= APRS_MON_REDRAW_MS))
	{
		aprsMonLastDraw = (now ? now : 1);
		aprsMonDraw();
	}
}

static void aprsMonAge(uint32_t ms, char *buf, size_t size)
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

// Copy up to APRS_MON_CHARS chars of 'text' starting at 'offset' into 'line'
static void aprsMonSlice(const char *text, uint32_t textLen, uint32_t offset, char *line)
{
	uint32_t n = 0;

	while ((n < APRS_MON_CHARS) && ((offset + n) < textLen))
	{
		line[n] = text[offset + n];
		n++;
	}
	line[n] = 0;
}

// Line with left and right parts, the left one truncated; inverted when selected
static void aprsMonPrintRow(int16_t y, char *left, const char *right, bool selected)
{
	size_t maxLeft = (APRS_MON_CHARS - strlen(right) - 1);

	if (strlen(left) > maxLeft)
	{
		left[maxLeft] = 0;
	}

	if (selected)
	{
		displayFillRect(0, y, DISPLAY_SIZE_X, FONT_SIZE_1_HEIGHT, false);
	}
	displayPrintCore(0, y, left, FONT_SIZE_1, TEXT_ALIGN_LEFT, selected);
	displayPrintCore(0, y, right, FONT_SIZE_1, TEXT_ALIGN_RIGHT, selected);
}

static void aprsMonDrawHint(const char *line1, const char *line2)
{
	displayPrintCentered((APRS_MON_LIST_Y + 24), line1, FONT_SIZE_3);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCentered((APRS_MON_LIST_Y + 56), line2, FONT_SIZE_1);
	displayPrintCentered((APRS_MON_LIST_Y + 66), "#:view 0:clear 9:exit", FONT_SIZE_1);
	displayThemeResetToDefault();
}

// --- Frames view ---------------------------------------------------------------
static void aprsMonDrawFrames(void)
{
	char line[APRS_MON_CHARS + 1];
	char age[8];
	char text[APRS_MON_CHARS * 2 + 1];
	uint32_t count = aprsRxHistoryCount();
	uint32_t now = ticksGetMillis();
	uint32_t first = ((aprsMonSelected >= APRS_MON_BLOCKS) ? (aprsMonSelected - APRS_MON_BLOCKS + 1) : 0);

	if (count == 0)
	{
		aprsMonDrawHint("Listening...", "FM, e.g. 144.800 MHz");
		return;
	}

	for (uint32_t b = 0; b < APRS_MON_BLOCKS; b++)
	{
		uint32_t idx = (first + b);
		const aprsRxFrame_t *f = aprsRxHistoryGet(idx);
		aprsRxParsed_t p;
		int16_t y = (APRS_MON_LIST_Y + (b * APRS_MON_BLOCK_H));

		if (f == NULL)
		{
			break;
		}

		aprsMonAge((now - f->timeMs), age, sizeof(age));

		if (aprsRxParse(f, &p))
		{
			snprintf(line, sizeof(line), "%s>%s", p.src, p.dst);
		}
		else
		{
			snprintf(line, sizeof(line), "(bad header)");
			p.info = f->data;
			p.infoLen = 0;
		}
		aprsMonPrintRow(y, line, age, (idx == aprsMonSelected));

		// Two lines of info
		uint32_t n = 0;
		for (uint16_t i = 0; (i < p.infoLen) && (n < (sizeof(text) - 1)); i++)
		{
			char c = (char)p.info[i];
			text[n++] = (((c >= 0x20) && (c <= 0x7E)) ? c : '.');
		}
		text[n] = 0;

		for (uint32_t l = 0; l < 2; l++)
		{
			aprsMonSlice(text, n, (l * APRS_MON_CHARS), line);
			displayPrintAt(0, (y + ((l + 1) * FONT_SIZE_1_HEIGHT)), line, FONT_SIZE_1);
		}
	}
}

// Raw view: TNC2 text lines, a separator, then the hex dump
static void aprsMonDrawRaw(void)
{
	char line[APRS_MON_CHARS + 1];
	const aprsRxFrame_t *f = aprsRxHistoryGet(aprsMonSelected);

	if (f == NULL)
	{
		aprsMonRawView = false;
		aprsMonDrawFrames();
		return;
	}

	uint32_t textLen = aprsRxFormatTnc2(f, aprsMonText, sizeof(aprsMonText));
	uint32_t textLines = ((textLen + APRS_MON_CHARS - 1) / APRS_MON_CHARS);
	uint32_t hexLines = ((f->len + APRS_MON_HEX_PER_LINE - 1) / APRS_MON_HEX_PER_LINE);
	uint32_t totalLines = (textLines + 1 + hexLines);
	uint32_t maxScroll = ((totalLines > APRS_MON_RAW_LINES) ? (totalLines - APRS_MON_RAW_LINES) : 0);

	if (aprsMonRawScroll > maxScroll)
	{
		aprsMonRawScroll = maxScroll;
	}

	for (uint32_t r = 0; r < APRS_MON_RAW_LINES; r++)
	{
		uint32_t l = (aprsMonRawScroll + r);
		int16_t y = (APRS_MON_LIST_Y + (r * FONT_SIZE_1_HEIGHT));

		if (l >= totalLines)
		{
			break;
		}

		if (l < textLines)
		{
			aprsMonSlice(aprsMonText, textLen, (l * APRS_MON_CHARS), line);
			displayPrintAt(0, y, line, FONT_SIZE_1);
		}
		else if (l == textLines)
		{
			snprintf(line, sizeof(line), "-- hex, %u bytes%s --", (unsigned)f->len, (f->truncated ? "+" : ""));
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayPrintCentered(y, line, FONT_SIZE_1);
			displayThemeResetToDefault();
		}
		else
		{
			uint32_t off = ((l - textLines - 1) * APRS_MON_HEX_PER_LINE);
			uint32_t n = 0;

			for (uint32_t i = 0; (i < APRS_MON_HEX_PER_LINE) && ((off + i) < f->len); i++)
			{
				n += snprintf(&line[n], (sizeof(line) - n), "%s%02X", (i ? " " : ""), f->data[off + i]);
			}
			displayPrintAt(0, y, line, FONT_SIZE_1);
		}
	}
}

// --- Stations / radar helpers -------------------------------------------------------
static uint32_t aprsMonStationIndex(void)
{
	for (uint32_t i = 0; i < aprsRxStationCount(); i++)
	{
		if (strcmp(aprsRxStationGet(i)->name, aprsMonStationName) == 0)
		{
			return i;
		}
	}
	return 0;
}

static void aprsMonStationSelect(uint32_t index)
{
	const aprsRxStation_t *st = aprsRxStationGet(index);

	if (st)
	{
		strcpy(aprsMonStationName, st->name);
	}
}

// Moves the station selection by 'dir'; for the radar only stations with a position count
static void aprsMonStationMove(int dir, bool needPos)
{
	int32_t count = (int32_t)aprsRxStationCount();
	int32_t i = (int32_t)aprsMonStationIndex();

	for (int32_t n = 0; n < count; n++)
	{
		i += dir;
		if ((i < 0) || (i >= count))
		{
			return; // no wrap
		}
		if ((needPos == false) || aprsRxStationGet(i)->hasPos)
		{
			aprsMonStationSelect(i);
			return;
		}
	}
}

// --- Stations view -------------------------------------------------------------------
#define APRS_MON_STATION_ROWS   ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - FONT_SIZE_1_HEIGHT - 1) / FONT_SIZE_1_HEIGHT)

static void aprsMonDrawStations(void)
{
	char left[APRS_MON_CHARS + 1];
	char right[APRS_MON_CHARS + 1];
	char age[8];
	char dist[12];
	uint32_t count = aprsRxStationCount();
	uint32_t now = ticksGetMillis();
	uint32_t sel = aprsMonStationIndex();
	uint32_t first = ((sel >= APRS_MON_STATION_ROWS) ? (sel - APRS_MON_STATION_ROWS + 1) : 0);
	double myLat, myLon;
	bool hasOwn = aprsRxOwnPosition(&myLat, &myLon);

	if (count == 0)
	{
		aprsMonDrawHint("No stations", "FM, e.g. 144.800 MHz");
		return;
	}

	for (uint32_t r = 0; r < APRS_MON_STATION_ROWS; r++)
	{
		const aprsRxStation_t *st = aprsRxStationGet(first + r);

		if (st == NULL)
		{
			break;
		}

		aprsMonAge((now - st->lastMs), age, sizeof(age));
		if (st->hasPos && hasOwn)
		{
			aprsRxFormatDistance(aprsRxDistanceKm(myLat, myLon, st->lat, st->lon), dist, sizeof(dist));
			snprintf(right, sizeof(right), "%s %03u %3s", dist, (unsigned)(aprsRxBearingDeg(myLat, myLon, st->lat, st->lon) + 0.5) % 360U, age);
		}
		else
		{
			snprintf(right, sizeof(right), "%s %3s", (st->hasPos ? "pos" : "-"), age);
		}
		snprintf(left, sizeof(left), "%s", st->name);
		aprsMonPrintRow((APRS_MON_LIST_Y + (r * FONT_SIZE_1_HEIGHT)), left, right, ((first + r) == sel));
	}

	// Details of the selected station
	const aprsRxStation_t *st = aprsRxStationGet(sel);
	if (st)
	{
		if (st->speedKmh >= 0)
		{
			snprintf(left, sizeof(left), "%ukm/h %03u  n:%u%s", (unsigned)st->speedKmh, (unsigned)st->courseDeg, (unsigned)st->count, (hasOwn ? "" : " no own pos"));
		}
		else
		{
			snprintf(left, sizeof(left), "n:%u%s", (unsigned)st->count, (st->hasPos ? (hasOwn ? "" : " no own pos") : " no position"));
		}
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintAt(0, (DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT), left, FONT_SIZE_1);
		displayThemeResetToDefault();
	}
}

// --- Radar view (north up, own position in the centre) --------------------------------
#define RADAR_R    48
#define RADAR_CX   (RADAR_R + 2)
#define RADAR_CY   (APRS_MON_LIST_Y + 1 + RADAR_R)
#define RADAR_TX   (RADAR_CX + RADAR_R + 6)

static void aprsMonDrawRadar(void)
{
	static const float ranges[] = { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
	char buf[APRS_MON_CHARS + 1];
	char age[8];
	double myLat, myLon;
	double maxDist = 0.0;
	uint32_t count = aprsRxStationCount();
	uint32_t sel = aprsMonStationIndex();
	uint32_t now = ticksGetMillis();

	if (aprsRxOwnPosition(&myLat, &myLon) == false)
	{
		aprsMonDrawHint("No own pos", "needs GPS fix/location");
		return;
	}

	for (uint32_t i = 0; i < count; i++)
	{
		const aprsRxStation_t *st = aprsRxStationGet(i);
		if (st->hasPos)
		{
			double d = aprsRxDistanceKm(myLat, myLon, st->lat, st->lon);
			if (d > maxDist)
			{
				maxDist = d;
			}
		}
	}

#if defined(APRS_MON_WAYPOINT)
	double wpLat, wpLon;
	bool hasWp = gpsWaypointGet(&wpLat, &wpLon);
	if (hasWp)
	{
		double d = aprsRxDistanceKm(myLat, myLon, wpLat, wpLon);
		if (d > maxDist)
		{
			maxDist = d;
		}
	}
#endif

	float range = ranges[(sizeof(ranges) / sizeof(ranges[0])) - 1];
	for (uint32_t i = 0; i < (sizeof(ranges) / sizeof(ranges[0])); i++)
	{
		if (maxDist <= ranges[i])
		{
			range = ranges[i];
			break;
		}
	}

	// Rings, north tick, own position
	displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);
	displayDrawCircle(RADAR_CX, RADAR_CY, RADAR_R, true);
	displayDrawCircle(RADAR_CX, RADAR_CY, (RADAR_R / 2), true);
	displayDrawFastVLine(RADAR_CX, (RADAR_CY - RADAR_R - 1), 5, true);
	displayDrawFastHLine((RADAR_CX - 2), RADAR_CY, 5, true);
	displayDrawFastVLine(RADAR_CX, (RADAR_CY - 2), 5, true);
	displayThemeResetToDefault();

	// Stations
	const aprsRxStation_t *selSt = aprsRxStationGet(sel);
	if (selSt && (selSt->hasPos == false))
	{
		selSt = NULL;
	}

	for (uint32_t i = 0; i < count; i++)
	{
		const aprsRxStation_t *st = aprsRxStationGet(i);

		if (st->hasPos == false)
		{
			continue;
		}

		if (selSt == NULL)
		{
			selSt = st; // nothing (with a position) selected yet: first one
			aprsMonStationSelect(i);
		}

		double d = aprsRxDistanceKm(myLat, myLon, st->lat, st->lon);
		double a = (aprsRxBearingDeg(myLat, myLon, st->lat, st->lon) * (M_PI / 180.0));
		double r = ((d / range) * RADAR_R);
		int16_t x = (int16_t)(RADAR_CX + (sin(a) * r));
		int16_t y = (int16_t)(RADAR_CY - (cos(a) * r));

		displayThemeApply(THEME_ITEM_FG_SATELLITE_COLOUR, THEME_ITEM_BG);
		displayFillCircle(x, y, 2, true);
		if (st == selSt)
		{
			displayDrawCircle(x, y, 5, true);
		}
		displayThemeResetToDefault();
	}

#if defined(APRS_MON_WAYPOINT)
	if (hasWp)
	{
		double d = aprsRxDistanceKm(myLat, myLon, wpLat, wpLon);
		double a = (aprsRxBearingDeg(myLat, myLon, wpLat, wpLon) * (M_PI / 180.0));
		double r = ((d / range) * RADAR_R);
		int16_t x = (int16_t)(RADAR_CX + (sin(a) * r));
		int16_t y = (int16_t)(RADAR_CY - (cos(a) * r));

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawRect((x - 3), (y - 3), 7, 7, true); // waypoint: square
		displayThemeResetToDefault();
	}
#endif

	// Selected station panel
	int16_t ty = APRS_MON_LIST_Y;
	if (selSt)
	{
		char dist[12];
		double d = aprsRxDistanceKm(myLat, myLon, selSt->lat, selSt->lon);

		displayPrintAt(RADAR_TX, ty, selSt->name, FONT_SIZE_1);
		aprsRxFormatDistance(d, dist, sizeof(dist));
		displayPrintAt(RADAR_TX, (ty + 10), dist, FONT_SIZE_1);
		snprintf(buf, sizeof(buf), "%03u deg", (unsigned)(aprsRxBearingDeg(myLat, myLon, selSt->lat, selSt->lon) + 0.5) % 360U);
		displayPrintAt(RADAR_TX, (ty + 20), buf, FONT_SIZE_1);
		aprsMonAge((now - selSt->lastMs), age, sizeof(age));
		snprintf(buf, sizeof(buf), "%s ago", age);
		displayPrintAt(RADAR_TX, (ty + 30), buf, FONT_SIZE_1);
		if (selSt->speedKmh >= 0)
		{
			snprintf(buf, sizeof(buf), "%ukm/h", (unsigned)selSt->speedKmh);
			displayPrintAt(RADAR_TX, (ty + 40), buf, FONT_SIZE_1);
		}
	}
	else
	{
		displayPrintAt(RADAR_TX, ty, "no pos", FONT_SIZE_1);
		displayPrintAt(RADAR_TX, (ty + 10), "yet", FONT_SIZE_1);
	}

	char rng[12];
	aprsRxFormatDistance(range, rng, sizeof(rng));
	snprintf(buf, sizeof(buf), "R %s", rng);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(RADAR_TX, (DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT), buf, FONT_SIZE_1);
	displayThemeResetToDefault();
}

// --- Messages view ---------------------------------------------------------------------
static void aprsMonDrawMessages(void)
{
	static const char *status[] = { "", "BLN", "...", "ACK", "REJ", "FAIL", "noTX" };
	char line[APRS_MON_CHARS + 1];
	char right[APRS_MON_CHARS + 1];
	char age[8];
	uint32_t count = aprsRxMessageCount();
	uint32_t now = ticksGetMillis();

	if (count == 0)
	{
		char own[APRS_RX_CALL_LEN + 4];
		bool canTx = aprsRxGetOwnCall(own, sizeof(own));

		snprintf(line, sizeof(line), "%s%s", (own[0] ? own : "no callsign"), (canTx ? "" : " (no APRS cfg)"));
		aprsMonDrawHint("No messages", line);
		return;
	}

	if (aprsMonMsgSelected >= count)
	{
		aprsMonMsgSelected = (count - 1);
	}

	uint32_t first = ((aprsMonMsgSelected >= APRS_MON_BLOCKS) ? (aprsMonMsgSelected - APRS_MON_BLOCKS + 1) : 0);

	for (uint32_t b = 0; b < APRS_MON_BLOCKS; b++)
	{
		const aprsRxMsgEntry_t *m = aprsRxMessageGet(first + b);
		int16_t y = (APRS_MON_LIST_Y + (b * APRS_MON_BLOCK_H));

		if (m == NULL)
		{
			break;
		}

		bool out = (m->kind >= APRS_MSG_OUT_PENDING);
		aprsMonAge((now - m->timeMs), age, sizeof(age));
		snprintf(line, sizeof(line), "%s%s", (out ? "> " : (m->kind == APRS_MSG_IN_BULLETIN ? "B " : "< ")), m->peer);
		if (m->kind == APRS_MSG_OUT_PENDING)
		{
			snprintf(right, sizeof(right), "%s%u %s", status[m->kind], (unsigned)m->tries, age);
		}
		else
		{
			snprintf(right, sizeof(right), "%s%s%s", status[m->kind], (status[m->kind][0] ? " " : ""), age);
		}
		aprsMonPrintRow(y, line, right, ((first + b) == aprsMonMsgSelected));

		uint32_t n = strlen(m->text);
		for (uint32_t l = 0; l < 2; l++)
		{
			aprsMonSlice(m->text, n, (l * APRS_MON_CHARS), line);
			displayPrintAt(0, (y + ((l + 1) * FONT_SIZE_1_HEIGHT)), line, FONT_SIZE_1);
		}
	}
}

// Reply / new message: canned texts
static void aprsMonDrawReply(void)
{
	char line[APRS_MON_CHARS + 1];
	uint32_t rows = ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - FONT_SIZE_1_HEIGHT - 2) / FONT_SIZE_1_HEIGHT);
	uint32_t first = ((aprsMonReplyIndex >= rows) ? (aprsMonReplyIndex - rows + 1) : 0);

	snprintf(line, sizeof(line), "To %s:", aprsMonReplyTo);
	displayPrintAt(0, APRS_MON_LIST_Y, line, FONT_SIZE_1);

	for (uint32_t r = 0; r < rows; r++)
	{
		uint32_t i = (first + r);

		if (i >= APRS_MON_NUM_REPLIES)
		{
			break;
		}
		snprintf(line, sizeof(line), "%s", aprsMonReplies[i]);
		aprsMonPrintRow((APRS_MON_LIST_Y + ((r + 1) * FONT_SIZE_1_HEIGHT) + 2), line, "", (i == aprsMonReplyIndex));
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(0, (DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT), "GREEN:send RED:cancel", FONT_SIZE_1);
	displayThemeResetToDefault();
}

// --- DTMF / CW views -----------------------------------------------------------------
static void aprsMonDrawToneText(bool cw)
{
	char text[APRS_RX_TONE_TEXT_LEN + 1];
	char line[24];
	uint32_t perLine = (DISPLAY_SIZE_X / 8); // FONT_SIZE_3: 8 px wide
	uint32_t lines = ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - FONT_SIZE_1_HEIGHT - 2) / FONT_SIZE_3_HEIGHT);
	uint32_t len = aprsRxToneText(cw, text, sizeof(text), NULL);

	if (cw)
	{
		uint8_t wpm = cwRxGetWpm();

		if (wpm)
		{
			snprintf(line, sizeof(line), "%uWPM %uHz %s", (unsigned)wpm, (unsigned)cwRxGetToneHz(), (cwRxIsKeyed() ? "#" : ""));
		}
		else
		{
			snprintf(line, sizeof(line), "CW: waiting...");
		}
	}
	else
	{
		snprintf(line, sizeof(line), "DTMF: %s", (len ? "" : "waiting..."));
	}
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(0, (DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT), line, FONT_SIZE_1);
	displayThemeResetToDefault();

	// Last 'lines' lines of text
	uint32_t maxChars = (perLine * lines);
	uint32_t start = ((len > maxChars) ? (len - maxChars) : 0);
	if (start)
	{
		start += ((perLine - ((len - start) % perLine)) % perLine); // keep line alignment stable
	}

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
		displayPrintAt(0, (APRS_MON_LIST_Y + (l * FONT_SIZE_3_HEIGHT)), line, FONT_SIZE_3);
	}
}

static void aprsMonSetView(aprsMonView_t view)
{
	aprsMonRawView = false;
	aprsMonView = view;
}

static void aprsMonDraw(void)
{
	static const char *titles[] = { "APRS frames", "APRS stations", "APRS radar", "APRS messages", "DTMF decoder", "CW decoder" };
	char line[APRS_MON_CHARS + 1];
	aprsRxStatus_t st;

	aprsRxGetStatus(&st);

	displayClearBuf();
	menuDisplayTitle(aprsMonReplyActive ? "APRS message" : (aprsMonRawView ? "APRS raw" : titles[aprsMonView]));

	switch (st.state)
	{
		case APRS_RX_STATE_LISTENING:
			if (aprsMonRawView)
			{
				snprintf(line, sizeof(line), "%u/%u  OK%u E%u", (unsigned)(aprsMonSelected + 1), (unsigned)aprsRxHistoryCount(),
						(unsigned)st.framesOk, (unsigned)st.crcErrors);
			}
			else
			{
				uint32_t unread = aprsRxMessageUnreadCount();

				snprintf(line, sizeof(line), "%ddBm L%u OK%u E%u", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY), (unsigned)st.levelPercent,
						(unsigned)st.framesOk, (unsigned)st.crcErrors);
				if (unread)
				{
					size_t l = strlen(line);
					snprintf(&line[l], (sizeof(line) - l), " M%u", (unsigned)unread);
				}
			}
			break;
		case APRS_RX_STATE_NO_CLOCK:
			snprintf(line, sizeof(line), "APRS RX: I2S no clock");
			break;
		default:
			snprintf(line, sizeof(line), (trxGetMode() != RADIO_MODE_ANALOG) ? "APRS RX: FM only" : "APRS RX: paused");
			break;
	}
	displayPrintAt(0, APRS_MON_STATUS_Y, line, FONT_SIZE_1);

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, (APRS_MON_LIST_Y - 2), DISPLAY_SIZE_X, true);
	displayThemeResetToDefault();

	if (aprsMonReplyActive)
	{
		aprsMonDrawReply();
	}
	else if (aprsMonRawView)
	{
		aprsMonDrawRaw();
	}
	else
	{
		switch (aprsMonView)
		{
			case APRS_MON_VIEW_MESSAGES:
				aprsRxMessagesMarkRead();
				aprsMonDrawMessages();
				break;
			case APRS_MON_VIEW_DTMF:
				aprsMonDrawToneText(false);
				break;
			case APRS_MON_VIEW_CW:
				aprsMonDrawToneText(true);
				break;
			case APRS_MON_VIEW_STATIONS:
				aprsMonDrawStations();
				break;
			case APRS_MON_VIEW_RADAR:
				aprsMonDrawRadar();
				break;
			default:
				aprsMonDrawFrames();
				break;
		}
	}

	displayRender();
}

// Returns true when the event was consumed by the monitor
static bool aprsMonHandleEvent(uiEvent_t *ev)
{
	if ((ev->events & KEY_EVENT) == 0)
	{
		return false;
	}

	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		return false; // SK2 + number: quickkeys
	}

	if (aprsMonReplyActive)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_UP) && (aprsMonReplyIndex > 0))
		{
			aprsMonReplyIndex--;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && ((aprsMonReplyIndex + 1) < APRS_MON_NUM_REPLIES))
		{
			aprsMonReplyIndex++;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			aprsRxSendMessage(aprsMonReplyTo, aprsMonReplies[aprsMonReplyIndex]);
			aprsMonReplyActive = false;
			aprsMonView = APRS_MON_VIEW_MESSAGES;
			aprsMonMsgSelected = 0;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			aprsMonReplyActive = false;
		}
		else
		{
			return true; // modal
		}
		aprsMonDraw();
		return true;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (aprsMonRawView == false))
	{
		const char *to = NULL;

		if (aprsMonView == APRS_MON_VIEW_MESSAGES)
		{
			const aprsRxMsgEntry_t *m = aprsRxMessageGet(aprsMonMsgSelected);
			if (m && (m->kind != APRS_MSG_IN_BULLETIN))
			{
				to = m->peer;
			}
		}
		else if ((aprsMonView == APRS_MON_VIEW_STATIONS) || (aprsMonView == APRS_MON_VIEW_RADAR))
		{
			const aprsRxStation_t *st = aprsRxStationGet(aprsMonStationIndex());
			if (st)
			{
				to = st->name;
			}
		}

		if (to == NULL)
		{
			return false; // GREEN: leave the screen
		}
		snprintf(aprsMonReplyTo, sizeof(aprsMonReplyTo), "%s", to);
		aprsMonReplyActive = true;
		aprsMonReplyIndex = 0;
		aprsMonDraw();
		return true;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_9))
	{
		aprsMonSetEnabled(false);
		displayClearBuf();
		menuDisplayTitle(currentLanguage->rssi);
		updateScreen(true, false);
		return true;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		aprsMonSetView((aprsMonView + 1) % APRS_MON_VIEW_MAX);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		if (aprsMonRawView)
		{
			aprsMonRawView = false;
		}
		else if ((aprsMonView == APRS_MON_VIEW_FRAMES) && (aprsRxHistoryCount() > 0))
		{
			aprsMonRawView = true;
			aprsMonRawScroll = 0;
		}
#if defined(APRS_MON_WAYPOINT)
		else if ((aprsMonView == APRS_MON_VIEW_STATIONS) || (aprsMonView == APRS_MON_VIEW_RADAR))
		{
			const aprsRxStation_t *st = aprsRxStationGet(aprsMonStationIndex());
			if (st && st->hasPos)
			{
				char buf[SCREEN_LINE_BUFFER_SIZE];

				gpsWaypointSet(st->lat, st->lon);
				snprintf(buf, sizeof(buf), "Waypoint: %s", st->name);
				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500U, buf, false);
				soundSetMelody(MELODY_ACK_BEEP);
			}
		}
#endif
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_0))
	{
		switch (aprsMonView)
		{
			case APRS_MON_VIEW_MESSAGES:
				aprsRxMessagesClear();
				aprsMonMsgSelected = 0;
				break;
			case APRS_MON_VIEW_DTMF:
			case APRS_MON_VIEW_CW:
				aprsRxToneTextClear();
				break;

			default:
				aprsRxHistoryClear();
				aprsMonRawView = false;
				aprsMonSelected = 0;
				aprsMonStationName[0] = 0;
				break;
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		int dir = (KEYCHECK_PRESS(ev->keys, KEY_UP) ? -1 : 1);

		if (aprsMonRawView)
		{
			if (dir < 0)
			{
				if (aprsMonRawScroll > 0)
				{
					aprsMonRawScroll--;
				}
			}
			else
			{
				aprsMonRawScroll++; // clamped when drawing
			}
		}
		else if (aprsMonView == APRS_MON_VIEW_MESSAGES)
		{
			if ((dir < 0) && (aprsMonMsgSelected > 0))
			{
				aprsMonMsgSelected--;
			}
			else if ((dir > 0) && ((aprsMonMsgSelected + 1) < aprsRxMessageCount()))
			{
				aprsMonMsgSelected++;
			}
		}
		else if ((aprsMonView == APRS_MON_VIEW_DTMF) || (aprsMonView == APRS_MON_VIEW_CW))
		{
			return false;
		}
		else if (aprsMonView == APRS_MON_VIEW_FRAMES)
		{
			if ((dir < 0) && (aprsMonSelected > 0))
			{
				aprsMonSelected--;
			}
			else if ((dir > 0) && ((aprsMonSelected + 1) < aprsRxHistoryCount()))
			{
				aprsMonSelected++;
			}
		}
		else
		{
			if (aprsMonStationName[0] == 0)
			{
				aprsMonStationSelect(0);
			}
			aprsMonStationMove(dir, (aprsMonView == APRS_MON_VIEW_RADAR));
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) && aprsMonRawView)
	{
		aprsMonRawView = false; // RED leaves the raw view first
	}
	else
	{
		return false;
	}

	aprsMonDraw();
	return true;
}
#endif

#endif
