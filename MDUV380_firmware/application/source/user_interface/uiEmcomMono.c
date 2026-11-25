/*
 * Fork: "EMCOM" screen style for the 128 x 64 monochrome display of the TYT MD-9600 / RT-90,
 * by Marcin SP7TQ. Same API as uiEmcom.c (the 160 x 128 colour version, RT3S), see uiEmcom.h.
 * The layouts follow the approved mockups (MD9600_firmware/tools/mockups):
 *
 *   0..8    status bar (inverse): state / mode, power, time slot ; lock, GPS satellites, supply
 *   10      zone (or VFO) | n/m
 *   19..34  channel name (8x16), or the VFO frequency, or the DMR talk group
 *   36      frequency, offset, tone / TA
 *   45      dotted divider
 *   47      last heard / tones / RX level bar
 *   56      clock ; locator, SOS, locks
 *
 * RX of a DMR call: the caller in an inverse band, name and place, S-meter at the bottom.
 * TX: an inverse band with the timer, power and mode, the time-out bar under it.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include <string.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiEmcom.h"
#include "user_interface/emcomDistance.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/codeplug.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "functions/aprs.h"
#include "functions/eventLog.h"
#include "interfaces/batteryAndPowerManagement.h"
#include "io/LEDs.h"
#include "io/keyboard.h"
#include "io/buttons.h"
#include "hardware/HR-C6000.h"
#include "functions/dmrSms.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(HAS_EMCOM_UI) && defined(EMCOM_UI_MONO)

#define W               DISPLAY_SIZE_X
#define H               DISPLAY_SIZE_Y
#define HEADER_H        9

typedef enum
{
	ST_STBY = 0,
	ST_RX,
	ST_TX,
	ST_SCAN
} emcomState_t;

static bool vfoAllowed = true;
static bool channelAllowed = true;
static emcomMainInfo_t lastInfo;
static bool lastInfoValid = false;
static bool inRender = false;
static uint32_t rxSinceMs = 0U;
static bool barVisible = false;  // the RX level bar is on the screen (RX panels)
static int16_t barX, barY, barW; // where

static struct
{
	bool     valid;
	uint8_t  state;
	uint8_t  radioMode;
	bool     isVFO;
	int16_t  zoneIndex;
	int16_t  channelIndex;
	uint32_t rxFreq;
	uint32_t txFreq;
	uint32_t minute;
	bool     gpsFix;
	uint16_t sats;
	bool     dmrCaller;
	bool     details;
	bool     sosPhase;
	bool     locks;
	uint32_t lastFull;
	int16_t  rxDbm;
	uint32_t rxSecs;
	uint32_t smsUnread;
} drawn;

// SOS
#define SOS_FIRST_INTERVAL_MS   60000U
#define SOS_INTERVAL_MS         120000U
#define SOS_FAST_BEACONS        3U
#define SOS_BUSY_RETRY_MS       20000U
static bool sosOn = false;
static uint32_t sosNextMs;
static uint32_t sosSent;
static bool sosPauseShown;

// --- drawing helpers (ST7567: a set pixel is dark) -----------------------------------------
static void box(int16_t x, int16_t y, int16_t w, int16_t h, bool on)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > W)
	{
		w = (W - x);
	}
	if ((y + h) > H)
	{
		h = (H - y);
	}
	if ((w <= 0) || (h <= 0))
	{
		return;
	}
	displayFillRect(x, y, w, h, (on == false));
}

static void hline(int16_t x, int16_t y, int16_t w)
{
	box(x, y, w, 1, true);
}

static void vline(int16_t x, int16_t y, int16_t h)
{
	box(x, y, 1, h, true);
}

static void frame(int16_t x, int16_t y, int16_t w, int16_t h)
{
	hline(x, y, w);
	hline(x, (y + h - 1), w);
	vline(x, y, h);
	vline((x + w - 1), y, h);
}

static void dotted(int16_t x, int16_t y, int16_t w)
{
	for (int16_t i = 0; i < w; i += 2)
	{
		displaySetPixel((x + i), y, true);
	}
}

static int16_t fontW(ucFont_t font)
{
	return ((font == FONT_SIZE_4) ? 16 : (((font == FONT_SIZE_3) || (font == FONT_SIZE_2)) ? 8 : 6));
}

typedef enum
{
	AL_L = 0,
	AL_R,  // x is the right edge
	AL_C   // centred on the screen
} align_t;

// inv: light text, the area behind it must already be dark
static int16_t txt(int16_t x, int16_t y, const char *str, ucFont_t font, bool inv, align_t al)
{
	int16_t w = (int16_t)(strlen(str) * fontW(font));

	if (al == AL_R)
	{
		x -= w;
	}
	else if (al == AL_C)
	{
		x = (int16_t)((W - w) / 2);
	}
	if (x < 0)
	{
		x = 0;
	}
	displayPrintCore(x, y, str, font, TEXT_ALIGN_LEFT, inv);
	return w;
}

// frame with a fill of 'frac' (0..1000), optional ticks above
static void levelBar(int16_t x, int16_t y, int16_t w, int16_t h, int permille, int ticks)
{
	frame(x, y, w, h);
	box((x + 1), (y + 1), (int16_t)(((w - 2) * CLAMP(permille, 0, 1000)) / 1000), (h - 2), true);
	for (int t = 1; t < ticks; t++)
	{
		displaySetPixel((int16_t)(x + ((w * t) / ticks)), (y - 1), true);
	}
}

// 7x7 icons, drawn light on the dark status bar
static const uint8_t ICON_SAT[7]  = { 0x38, 0x44, 0x92, 0xBA, 0x92, 0x44, 0x38 };
static const uint8_t ICON_PLUG[7] = { 0x50, 0x50, 0xF8, 0xF8, 0x70, 0x20, 0x20 };
static const uint8_t ICON_LOCK[7] = { 0x70, 0x88, 0x88, 0xF8, 0xD8, 0xF8, 0x00 };
static const uint8_t ICON_MAIL[7] = { 0x00, 0xFE, 0xC6, 0xAA, 0x92, 0x82, 0xFE };

static void icon7(int16_t x, int16_t y, const uint8_t *rows, bool on)
{
	for (int16_t r = 0; r < 7; r++)
	{
		for (int16_t c = 0; c < 7; c++)
		{
			if (rows[r] & (0x80 >> c))
			{
				displaySetPixel((x + c), (y + r), on);
			}
		}
	}
}

// --- state ----------------------------------------------------------------------------------
emcomStyle_t emcomStyleGet(void)
{
	// The MD-9600 has the classic screen and one EMCOM style (no colours to vary)
	return (settingsIsOptionBitSet(BIT_UI_CLASSIC_STYLE) ? EMCOM_STYLE_CLASSIC : EMCOM_STYLE_EMCOM);
}

void emcomStyleSet(emcomStyle_t style)
{
	settingsSetOptionBit(BIT_UI_CLASSIC_STYLE, (style == EMCOM_STYLE_CLASSIC));
	settingsSetOptionBit(BIT_UI_EMCOM_MONO, false);
	settingsSetOptionBit(BIT_UI_EMCOM_CONTRAST, false);
}

const char *emcomStyleName(emcomStyle_t style)
{
	return ((style == EMCOM_STYLE_CLASSIC) ? "Classic" : "EMCOM");
}

bool emcomStyleEnabled(void)
{
	return (settingsIsOptionBitSet(BIT_UI_CLASSIC_STYLE) == false);
}

bool emcomShapeCues(void)
{
	return true; // monochrome: the state is always shown by a word and a shape
}

void emcomApplyStyle(void)
{
	drawn.valid = false;
}

void emcomSetVfoAllowed(bool allowed)
{
	vfoAllowed = allowed;
}

void emcomSetChannelAllowed(bool allowed)
{
	channelAllowed = allowed;
}

bool emcomScreenActive(void)
{
	int menu = menuSystemGetCurrentMenuNumber();
	int root = menuSystemGetRootMenuNumber();

	if (emcomStyleEnabled() == false)
	{
		return false;
	}
	if ((menu != UI_CHANNEL_MODE) && (menu != UI_VFO_MODE) && (menu != UI_TX_SCREEN))
	{
		return false;
	}
	if ((menu == UI_TX_SCREEN) && (menuTxScreenDisplaysLastHeard() || menuTxScreenIsSatellite()))
	{
		return false;
	}
	if (root == UI_VFO_MODE)
	{
		return vfoAllowed;
	}
	return ((root == UI_CHANNEL_MODE) && channelAllowed);
}

static emcomState_t currentState(void)
{
	if (isTransmittingIgnoringAPRSBeaconing())
	{
		return ST_TX;
	}
	if (uiDataGlobal.Scan.active)
	{
		return ST_SCAN;
	}
	if (LedRead(LED_GREEN) || ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) != 0))
	{
		return ST_RX;
	}
	return ST_STBY;
}

static bool gpsHasFix(void)
{
#if defined(HAS_GPS)
	return ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) && ((gpsData.Status & GPS_STATUS_HAS_FIX) != 0));
#else
	return false;
#endif
}

static uint16_t gpsSats(void)
{
#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON)
	{
		return (uint16_t)(gpsData.SatsInViewGP + gpsData.SatsInViewBD);
	}
#endif
	return 0U;
}

static bool ownPosition(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return emcomCoordinateIsValid(*lat, *lon);
	}
#endif
	if (settingsLocationIsValid())
	{
		*lat = settingsLocationGetLatitude();
		*lon = settingsLocationGetLongitude();
		return emcomCoordinateIsValid(*lat, *lon);
	}
	return false;
}

static bool dmrCallerActive(void)
{
	return ((trxGetMode() == RADIO_MODE_DIGITAL) && uiDataGlobal.isDisplayingQSOData &&
			(LinkHead != NULL) && (HRC6000GetReceivedTgOrPcId() != 0));
}

// --- text helpers ---------------------------------------------------------------------------
static void toneString(uint16_t tone, char *buf, size_t size)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	if (type == CSS_TYPE_CTCSS)
	{
		snprintf(buf, size, "%u.%u", (unsigned)(tone / 10), (unsigned)(tone % 10));
	}
	else if (type & CSS_TYPE_DCS)
	{
		dcsPrintf(buf, size, NULL, tone);
	}
	else
	{
		snprintf(buf, size, "-");
	}
}

static void formatFreq(uint32_t freq, char *buf, size_t size)
{
	uint32_t dec = (freq % 100000U);

	if ((dec % 10U) != 0U)
	{
		snprintf(buf, size, "%u.%05u", (unsigned)(freq / 100000U), (unsigned)dec);
	}
	else
	{
		snprintf(buf, size, "%u.%04u", (unsigned)(freq / 100000U), (unsigned)(dec / 10U));
	}
}

// "-0.6" / "+7.6" / "" (MHz, kHz digits only when needed)
static void offsetString(char *buf, size_t size)
{
	int32_t offset = ((int32_t)currentChannelData->txFreq - (int32_t)currentChannelData->rxFreq) / 100; // kHz
	int32_t a = abs(offset);

	if (offset == 0)
	{
		buf[0] = 0;
	}
	else if ((a % 100) == 0)
	{
		snprintf(buf, size, "%c%d.%d", ((offset < 0) ? '-' : '+'), (int)(a / 1000), (int)((a % 1000) / 100));
	}
	else
	{
		snprintf(buf, size, "%c%d.%03d", ((offset < 0) ? '-' : '+'), (int)(a / 1000), (int)(a % 1000));
	}
}

static void mmss(int secs, char *buf, size_t size)
{
	secs = ((secs < 0) ? 0 : secs);
	if (secs >= 3600)
	{
		snprintf(buf, size, "%d:%02d:%02d", (secs / 3600), ((secs / 60) % 60), (secs % 60));
	}
	else
	{
		snprintf(buf, size, "%d:%02d", (secs / 60), (secs % 60));
	}
}

static void ageString(uint32_t ms, char *buf, size_t size)
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

static void sUnits(int dBm, char *buf, size_t size)
{
	if (dBm >= SMETER_S9)
	{
		int over = (dBm - SMETER_S9);

		if (over >= 10)
		{
			snprintf(buf, size, "S9+%d", ((over / 10) * 10));
		}
		else
		{
			snprintf(buf, size, "S9");
		}
	}
	else
	{
		snprintf(buf, size, "S%d", CLAMP(((dBm - SMETER_S0) / 4), 0, 9));
	}
}

// 0..1000: S0..S9 = 0..750, S9..S9+60 = 750..1000
static int rssiPermille(int dBm)
{
	if (dBm <= SMETER_S0)
	{
		return 0;
	}
	if (dBm <= SMETER_S9)
	{
		return (((dBm - SMETER_S0) * 750) / (SMETER_S9 - SMETER_S0));
	}
	return CLAMP((750 + (((dBm - SMETER_S9) * 250) / 60)), 0, 1000);
}

static void clockString(char *buf, size_t size)
{
	if (uiDataGlobal.dateTimeSecs <= 86400U)
	{
		snprintf(buf, size, (((nonVolatileSettings.timezone & 0x80) == 0) ? "--:--Z" : "--:--"));
		return;
	}

	struct tm t;
	time_t_custom secs = uiDataGlobal.dateTimeSecs;

	gmtime_r_Custom(&secs, &t);
	if ((nonVolatileSettings.timezone & 0x80) != 0)
	{
		struct tm l;
		int32_t offset = (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60));
		time_t_custom localSecs = (time_t_custom)((int64_t)secs + offset);

		gmtime_r_Custom(&localSecs, &l);
		if (settingsIsOptionBitSet(BIT_CLOCK_LOCAL_AND_ZULU))
		{
			snprintf(buf, size, "%02d:%02d/%02d:%02dZ", l.tm_hour, l.tm_min, t.tm_hour, t.tm_min);
		}
		else
		{
			snprintf(buf, size, "%02d:%02d", l.tm_hour, l.tm_min);
		}
		return;
	}
	snprintf(buf, size, "%02d:%02dZ", t.tm_hour, t.tm_min);
}

// "LABEL value": label regular, value bold (the same on every screen)
static int16_t kv(int16_t x, int16_t y, const char *label, const char *value)
{
	int16_t w = (int16_t)(txt(x, y, label, FONT_SIZE_1, false, AL_L) + 3);

	return (int16_t)(w + txt((int16_t)(x + w), y, value, FONT_SIZE_1_BOLD, false, AL_L));
}

// --- status bar -------------------------------------------------------------------------------
static void drawStatusBar(emcomState_t st)
{
	char buf[24];
	char left[24];
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	uint8_t powerLevel = trxGetPowerLevel();
	int16_t r = (W - 1);

	box(0, 0, W, HEADER_H, true);

	if (sosOn && (st != ST_TX))
	{
		if (((ticksGetMillis() / 600U) & 1U) == 0U)
		{
			txt(0, 1, "EMERGENCY SOS", FONT_SIZE_1_BOLD, true, AL_C);
		}
		return;
	}

	// Right: supply voltage, plug, GPS satellites, lock
	int volts, mvolts;

	getBatteryVoltage(&volts, &mvolts);
	snprintf(buf, sizeof(buf), "%d.%dV", volts, mvolts);
	if (batteryIsLowWarning() && (((ticksGetMillis() / 500U) & 1U) != 0U))
	{
		r -= (int16_t)(strlen(buf) * 6); // low supply: blinks
	}
	else
	{
		r -= txt(r, 1, buf, FONT_SIZE_1, true, AL_R);
	}
	r -= 8;
	icon7(r, 1, ICON_PLUG, false);
	r -= 1;

#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON)
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)gpsSats());
		r -= txt(r, 1, buf, (gpsHasFix() ? FONT_SIZE_1_BOLD : FONT_SIZE_1), true, AL_R) + 1;
		r -= 7;
		icon7(r, 1, ICON_SAT, false);
		r -= 2;
	}
#endif
	if (keypadLocked || PTTLocked)
	{
		r -= 7;
		icon7(r, 1, ICON_LOCK, false);
		r -= 2;
	}
#if defined(HAS_DMR_SMS)
	uint32_t unread = dmrSmsUnreadCount();
	if (unread > 0U)
	{
		// unread DMR SMS: envelope and the count, as a light chip on the dark bar
		snprintf(buf, sizeof(buf), "%u", (unsigned)((unread > 9U) ? 9U : unread));
		int16_t cw = (int16_t)(7 + 1 + (strlen(buf) * 6) + 3);

		r -= cw;
		box(r, 0, cw, HEADER_H, false);
		icon7((r + 1), 1, ICON_MAIL, true);
		txt((r + 9), 1, buf, FONT_SIZE_1_BOLD, false, AL_L);
		r -= 2;
	}
#endif

	// Left: state and mode
	const char *prefix = ((st == ST_RX) ? "RX " : ((st == ST_SCAN) ? "SCAN " : ""));

	if (digital)
	{
		if ((st == ST_RX) && (HRC6000GetReceivedTgOrPcId() != 0))
		{
			uint32_t id = HRC6000GetReceivedTgOrPcId();

			snprintf(left, sizeof(left), "RX TS%d %s%u", (trxGetDMRTimeSlot() + 1), (((id >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), (unsigned)(id & 0xFFFFFF));
		}
		else
		{
			snprintf(left, sizeof(left), "%sDMR TS%d CC%u", prefix, (trxGetDMRTimeSlot() + 1), (unsigned)trxGetDMRColourCode());
		}
	}
	else
	{
		snprintf(left, sizeof(left), "%s%s %s%s", prefix, (trxGetBandwidthIs25kHz() ? "FM" : "FMN"), getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
	}

	size_t maxChars = (size_t)((r - 2) / 6);
	if (strlen(left) > maxChars)
	{
		left[maxChars] = 0;
	}
	txt(1, 1, left, FONT_SIZE_1_BOLD, true, AL_L);
}

void emcomRenderStatusBar(bool forceBatteryDisplay)
{
	UNUSED_PARAMETER(forceBatteryDisplay);

	// The caller cleared (part of) the rows under the status bar too: draw the whole screen again
	if ((inRender == false) && lastInfoValid && emcomScreenActive())
	{
		emcomMainInfo_t info = lastInfo;

		emcomRenderMain(&info);
		return;
	}
	drawStatusBar(currentState());
}

// --- level meter --------------------------------------------------------------------------------
// Segmented S-meter: 3 px bars every 4 px rising to the right, the unlit ones a baseline dot
static void smeter(int16_t x, int16_t y, int16_t w, int permille)
{
	int n = (w / 4);
	int lit = ((n * CLAMP(permille, 0, 1000)) + 500) / 1000;

	for (int i = 0; i < n; i++)
	{
		int16_t h = (int16_t)(2 + ((i * 5) / ((n > 1) ? (n - 1) : 1)));

		if (i < lit)
		{
			box((int16_t)(x + (i * 4)), (int16_t)(y + 7 - h), 3, h, true);
		}
		else
		{
			displaySetPixel((int16_t)(x + (i * 4) + 1), (y + 6), true);
		}
	}
}

void emcomDrawBar(int barWidth, bool isTx)
{
	UNUSED_PARAMETER(barWidth);
	UNUSED_PARAMETER(isTx);
}

void emcomDrawRssi(int dBm)
{
	if ((inRender == false) && barVisible && emcomScreenActive())
	{
		box(barX, barY, barW, 8, false);
		smeter(barX, barY, barW, rssiPermille(dBm));
		displayRenderRows((barY / 8), (((barY + 7) / 8) + 1));
	}
}

// --- main screen, layout "A" ------------------------------------------------------------------------
//   0..8    status bar (inverse)
//  10..33   big channel number (16x32) | zone, channel name (8x16)    VFO: big letter | VFO row, frequency
//  35       line
//  37       frequency, offset ; tone / contact
//  46       last heard, tones / squelch, or the S-meter
//  55..63   bottom bar (inverse): clock and date ; locator / state
#define A_TOP_Y     11
#define A_NAME_Y    20
#define A_LINE_Y    35
#define A_FREQ_Y    37
#define A_INFO_Y    46
#define A_BAR_Y     55

// Bottom bar: 'left' text (NULL: clock and date) and the state / locator on the right
static void drawBottomBar(emcomState_t st, const char *left, const char *right)
{
	char buf[24];
	int16_t r = (W - 2);
	int16_t leftEnd;

	box(0, A_BAR_Y, W, (H - A_BAR_Y), true);

	if (left)
	{
		leftEnd = (int16_t)(1 + txt(1, (A_BAR_Y + 1), left, FONT_SIZE_1, true, AL_L));
	}
	else
	{
		clockString(buf, sizeof(buf));
		leftEnd = (int16_t)(1 + txt(1, (A_BAR_Y + 1), buf, FONT_SIZE_1_BOLD, true, AL_L));
		if ((uiDataGlobal.dateTimeSecs > 86400U) && (strlen(buf) <= 6))
		{
			static const char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
			struct tm t;
			time_t_custom secs = uiDataGlobal.dateTimeSecs;

			if ((nonVolatileSettings.timezone & 0x80) != 0)
			{
				secs = (time_t_custom)((int64_t)secs + ((((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60))));
			}
			gmtime_r_Custom(&secs, &t);
			snprintf(buf, sizeof(buf), "%02d%s", t.tm_mday, months[CLAMP(t.tm_mon, 0, 11)]);
			leftEnd = (int16_t)(leftEnd + 6 + txt((int16_t)(leftEnd + 6), (A_BAR_Y + 1), buf, FONT_SIZE_1, true, AL_L));
		}
	}

	if (right)
	{
		txt(r, (A_BAR_Y + 1), right, FONT_SIZE_1_BOLD, true, AL_R);
		return;
	}

	// states first, the locator when there is room
	const char *state = NULL;

	if (sosOn)
	{
		state = "SOS";
	}
	else if (st == ST_SCAN)
	{
		state = "SCAN";
	}
	else if (settingsIsOptionBitSet(BIT_TX_INHIBIT))
	{
		state = "TX OFF";
	}
	else if (keypadLocked || PTTLocked)
	{
		state = ((keypadLocked && PTTLocked) ? "LOCK" : (keypadLocked ? "KEYL" : "PTTL"));
	}
	if (state)
	{
		// a light chip on the dark bar: the state stands out from the clock / locator
		int16_t w = (int16_t)((strlen(state) * 6) + 3);

		box((r - w + 2), (A_BAR_Y + 1), w, 8, false);
		txt(r, (A_BAR_Y + 1), state, FONT_SIZE_1_BOLD, false, AL_R);
		r -= (w + 3);
	}

	double lat, lon;
	if (ownPosition(&lat, &lon))
	{
		char loc[8];

		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		if ((r - (int16_t)(strlen(loc) * 6)) > (leftEnd + 3))
		{
			txt(r, (A_BAR_Y + 1), loc, (gpsHasFix() ? FONT_SIZE_1_BOLD : FONT_SIZE_1), true, AL_R);
		}
	}
#if defined(HAS_GPS)
	else if ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) && ((r - (6 * 6)) > (leftEnd + 3)))
	{
		txt(r, (A_BAR_Y + 1), "NO FIX", FONT_SIZE_1, true, AL_R);
	}
#endif
}

// Talk group / contact name of the channel (DMR)
static void contactName(char *buf, size_t size, uint32_t *id)
{
	uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));
	char contact[24];

	codeplugUtilConvertBufToString(currentContactData.name, contact, 16);
	if (nonVolatileSettings.overrideTG != 0)
	{
		uiUtilityBuildTgOrPCDisplayName(contact, sizeof(contact));
	}
	contact[16] = 0;
	snprintf(buf, size, "%s", contact);
	*id = PCorTG;
}

static uint8_t squelchPercent(void)
{
	uint8_t sql = ((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql);

	return (uint8_t)((sql > 0) ? (5 * (sql - 1)) : 0);
}

// Text cut to the pixels available at x (6 or 8 px font)
static void fitText(int16_t x, int16_t y, const char *str, ucFont_t font, bool inv)
{
	char buf[24];
	int16_t cw = fontW(font);
	size_t maxChars = (size_t)(((W - x) > 0) ? ((W - x) / cw) : 0);

	snprintf(buf, sizeof(buf), "%s", str);
	if (strlen(buf) > maxChars)
	{
		buf[maxChars] = 0;
	}
	txt(x, y, buf, font, inv, AL_L);
}

// The big block: channel number or VFO letter (16x32), a rule, the two lines beside it.
// Returns the x of the text beside it.
static int16_t drawBigBlock(const char *big, const char *line1, const char *line2)
{
	int16_t bw = (int16_t)(strlen(big) * 16);
	int16_t tx;

	txt(-1, 4, big, FONT_SIZE_4, false, AL_L); // glyph rows 10..28
	vline((bw + 1), A_TOP_Y, 22);
	tx = (int16_t)(bw + 5);
	if (line1)
	{
		fitText(tx, A_TOP_Y, line1, FONT_SIZE_1, false);
	}
	if (line2)
	{
		fitText(tx, A_NAME_Y, line2, FONT_SIZE_3, false);
	}
	return tx;
}

static void drawTones(int16_t y)
{
	char rx[10];
	char tx[10];
	uint16_t rxT = currentChannelData->rxTone;
	uint16_t txT = currentChannelData->txTone;

	toneString(rxT, rx, sizeof(rx));
	toneString(txT, tx, sizeof(tx));
	if ((rxT == CODEPLUG_CSS_TONE_NONE) && (txT == CODEPLUG_CSS_TONE_NONE))
	{
		kv(0, y, "TONE", "off");
	}
	else if (rxT == txT)
	{
		kv(0, y, ((codeplugGetCSSType(txT) & CSS_TYPE_DCS) ? "DCS" : "CTCSS"), tx);
	}
	else
	{
		int16_t w = kv(0, y, "RX", rx);
		kv((int16_t)(w + 6), y, "TX", tx);
	}
}

static void rightKv(int16_t y, const char *label, const char *value)
{
	kv((int16_t)(W - ((strlen(label) + strlen(value)) * 6) - 3), y, label, value);
}

// Channel / VFO screen, standby or FM reception
static void drawMain(const emcomMainInfo_t *info, emcomState_t st)
{
	char buf[28];
	char big[8];
	char line1[24];
	char line2[24];
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	uint32_t rxFreq = currentChannelData->rxFreq;
	uint32_t txFreq = currentChannelData->txFreq;

	if (uiDataGlobal.reverseRepeaterChannel)
	{
		uint32_t t = rxFreq;
		rxFreq = txFreq;
		txFreq = t;
	}
	if (uiDataGlobal.talkaround)
	{
		txFreq = rxFreq;
	}

	// --- big block
	if (info->isVFO)
	{
		snprintf(big, sizeof(big), "%c", info->vfoLetter);
		snprintf(line1, sizeof(line1), "VFO  %s", (info->freqEntry ? (info->entryTx ? "ENTER TX" : "ENTER RX") : (trxGetBandwidthIs25kHz() ? "25k" : "12.5k")));
		if (info->freqEntry)
		{
			const char *d = info->freqEntry;
			snprintf(line2, sizeof(line2), "%c%c%c.%c%c%c%c%c", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
		}
		else
		{
			formatFreq((info->entryTx ? txFreq : rxFreq), line2, sizeof(line2));
		}
		int16_t tx = drawBigBlock(big, line1, line2);

		if ((info->freqEntry == NULL) && (digital == false))
		{
			snprintf(buf, sizeof(buf), "%d", (int)squelchPercent());
			rightKv(A_TOP_Y, "SQL", buf);
		}
		if (info->freqEntry && (info->freqEntryIndex < 8))
		{
			int idx = info->freqEntryIndex;
			box((int16_t)(tx + ((idx + ((idx > 2) ? 1 : 0)) * 8)), (A_NAME_Y + 14), 7, 1, true);
		}
	}
	else if (info->gotoChannel > 0)
	{
		snprintf(big, sizeof(big), "%d", info->gotoChannel);
		drawBigBlock(big, "GO TO CHANNEL", "GRN go");
	}
	else
	{
		// channel number, 1-3 digits in the big font (the name keeps at least 8 characters)
		snprintf(big, sizeof(big), "%02d", info->channelNumber);
		if (strlen(big) > 3)
		{
			big[0] = 0;
		}
		snprintf(line1, sizeof(line1), "%s", (info->zoneName ? info->zoneName : ""));
		if (digital)
		{
			uint32_t id;
			char name[24];

			contactName(name, sizeof(name), &id);
			snprintf(line2, sizeof(line2), "%s %u", (((id >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), (unsigned)(id & 0xFFFFFF));
		}
		else
		{
			codeplugUtilConvertBufToString(currentChannelData->name, line2, 16);
		}
		if (big[0])
		{
			drawBigBlock(big, line1, line2);
		}
		else
		{
			// 4+ digit channel numbers: the number in the zone row, the name full width
			snprintf(buf, sizeof(buf), "%d %s", info->channelNumber, line1);
			fitText(0, A_TOP_Y, buf, FONT_SIZE_1, false);
			fitText(0, A_NAME_Y, line2, FONT_SIZE_3, false);
		}
	}

	hline(0, A_LINE_Y, W);

	if (info->freqEntry || (info->gotoChannel > 0))
	{
		txt(0, A_FREQ_Y, "GRN OK   A CANCEL", FONT_SIZE_1, false, AL_L);
		drawBottomBar(st, NULL, NULL);
		return;
	}

	// --- frequency row
	if (info->isVFO)
	{
		formatFreq((info->entryTx ? rxFreq : txFreq), line1, sizeof(line1));
		kv(0, A_FREQ_Y, (info->entryTx ? "RX" : "TX"), line1);
		if (digital)
		{
			snprintf(buf, sizeof(buf), "CC%u TS%d", (unsigned)trxGetDMRColourCode(), (trxGetDMRTimeSlot() + 1));
		}
		else
		{
			offsetString(buf, sizeof(buf));
		}
		txt((W - 1), A_FREQ_Y, buf, FONT_SIZE_1, false, AL_R);
	}
	else
	{
		formatFreq(((st == ST_TX) ? txFreq : rxFreq), buf, sizeof(buf));
		int16_t w = txt(0, A_FREQ_Y, buf, FONT_SIZE_1_BOLD, false, AL_L);
		char ofs[12];

		offsetString(ofs, sizeof(ofs));
		if (ofs[0] && (uiDataGlobal.talkaround == false))
		{
			w = (int16_t)(w + 5 + txt((int16_t)(w + 5), A_FREQ_Y, ofs, FONT_SIZE_1, false, AL_L));
		}

		buf[0] = 0;
		if (digital)
		{
			uint32_t id;
			char name[24];

			contactName(name, sizeof(name), &id);
			snprintf(buf, sizeof(buf), "%s", (uiDataGlobal.talkaround ? "TA" : (uiDataGlobal.reverseRepeaterChannel ? "REV" : name)));
		}
		else if (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE)
		{
			char t[10];

			toneString(currentChannelData->rxTone, t, sizeof(t));
			snprintf(buf, sizeof(buf), "%s%s", ((codeplugGetCSSType(currentChannelData->rxTone) & CSS_TYPE_DCS) ? "" : "T"), t);
		}
		size_t maxChars = (size_t)(((W - 1 - w - 6) > 0) ? ((W - 1 - w - 6) / 6) : 0);
		if (strlen(buf) > maxChars)
		{
			buf[maxChars] = 0;
		}
		txt((W - 1), A_FREQ_Y, buf, FONT_SIZE_1, false, AL_R);
	}

	// --- info row
	if ((st == ST_RX) && (digital == false))
	{
		int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

		sUnits(dBm, buf, sizeof(buf));
		int16_t w = txt(0, A_INFO_Y, buf, FONT_SIZE_1_BOLD, false, AL_L);
		barX = (int16_t)(w + 4);
		barY = A_INFO_Y;
		barW = (int16_t)(W - barX);
		smeter(barX, barY, barW, rssiPermille(dBm));
		barVisible = true;

		mmss((int)((ticksGetMillis() - rxSinceMs) / 1000U), line1, sizeof(line1));
		snprintf(line2, sizeof(line2), "RX %s", line1);
		snprintf(buf, sizeof(buf), "%ddBm", dBm);
		drawBottomBar(st, line2, buf);
		return;
	}

	if (digital && (uiDataGlobal.lastHeardCount > 0) && (LinkHead != NULL))
	{
		char call[SCREEN_LINE_BUFFER_SIZE];
		char age[8];

		getCallsignOnly(call, (((LinkHead->talkerAlias[0] != 0) && (strncmp(LinkHead->contact, "ID:", 3) == 0)) ? LinkHead->talkerAlias : LinkHead->contact));
		call[10] = 0;
		ageString((ticksGetMillis() - LinkHead->time), age, sizeof(age));
		kv(0, A_INFO_Y, "LH", call);
		txt((W - 1), A_INFO_Y, age, FONT_SIZE_1, false, AL_R);
	}
	else if (digital)
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)trxGetDMRColourCode());
		int16_t w = kv(0, A_INFO_Y, "CC", buf);
		snprintf(buf, sizeof(buf), "%d", (trxGetDMRTimeSlot() + 1));
		kv((int16_t)(w + 8), A_INFO_Y, "TS", buf);
		txt((W - 1), A_INFO_Y, "no calls yet", FONT_SIZE_1, false, AL_R);
	}
	else
	{
		drawTones(A_INFO_Y);
		if (info->isVFO)
		{
			if (currentChannelData->tot != 0)
			{
				uint32_t secs = (currentChannelData->tot * 15U);
				snprintf(buf, sizeof(buf), "%u%c", (unsigned)((secs < 60U) ? secs : ((secs + 30U) / 60U)), ((secs < 60U) ? 's' : 'm'));
				rightKv(A_INFO_Y, "TOT", buf);
			}
		}
		else
		{
			snprintf(buf, sizeof(buf), "%d", (int)squelchPercent());
			rightKv(A_INFO_Y, "SQL", buf);
		}
	}

	drawBottomBar(st, NULL, NULL);
}

// SK1 held: RX / TX frequencies with their tones (FM) or CC / TS (DMR) under the line
static void drawDetails(void)
{
	char f[16];
	char t[12];
	char buf[28];
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);

	box(0, (A_LINE_Y + 1), W, (H - A_LINE_Y - 1), false);
	formatFreq(currentChannelData->rxFreq, f, sizeof(f));
	toneString(currentChannelData->rxTone, t, sizeof(t));
	snprintf(buf, sizeof(buf), "%s %s", f, (digital ? "" : t));
	kv(0, A_FREQ_Y, "RX", buf);
	formatFreq(currentChannelData->txFreq, f, sizeof(f));
	toneString(currentChannelData->txTone, t, sizeof(t));
	snprintf(buf, sizeof(buf), "%s %s", f, (digital ? "" : t));
	kv(0, A_INFO_Y, "TX", buf);
	if (digital)
	{
		snprintf(buf, sizeof(buf), "CC%u TS%d", (unsigned)trxGetDMRColourCode(), (trxGetDMRTimeSlot() + 1));
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s SQL%d", (trxGetBandwidthIs25kHz() ? "25k" : "12.5k"), (int)squelchPercent());
	}
	drawBottomBar(ST_STBY, buf, "SK1");
}

// DMR caller: call sign, name and place, S-meter; talk group and the time on air in the bottom bar
static void drawDmrCaller(void)
{
	char buf[40];
	char call[SCREEN_LINE_BUFFER_SIZE];
	bool useTA = ((LinkHead->talkerAlias[0] != 0) &&
			((nonVolatileSettings.contactDisplayPriority == CONTACT_DISPLAY_PRIO_TA_CC_DB) ||
					(nonVolatileSettings.contactDisplayPriority == CONTACT_DISPLAY_PRIO_TA_DB_CC) ||
					(strncmp(LinkHead->contact, "ID:", 3) == 0)));
	const char *src = (useTA ? LinkHead->talkerAlias : LinkHead->contact);

	getCallsignOnly(call, src);
	call[16] = 0;
	txt(0, 11, call, FONT_SIZE_3, false, AL_C);

	// the rest of the contact / TA text: name and place on one line
	const char *rest = src;
	while (*rest && (*rest != ' '))
	{
		rest++;
	}
	while (*rest == ' ')
	{
		rest++;
	}
	snprintf(buf, sizeof(buf), "%s", rest);
	buf[21] = 0;
	txt(0, 27, buf, FONT_SIZE_1_BOLD, false, AL_C);

	hline(0, A_LINE_Y, W);

	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	sUnits(dBm, buf, sizeof(buf));
	int16_t w = txt(0, (A_LINE_Y + 4), buf, FONT_SIZE_1_BOLD, false, AL_L);
	barX = (int16_t)(w + 4);
	barY = (A_LINE_Y + 4);
	barW = (int16_t)(W - barX);
	smeter(barX, barY, barW, rssiPermille(dBm));
	barVisible = true;

	char tg[24];
	uint32_t id = LinkHead->talkGroupOrPcId;

	if ((id >> 24) == PC_CALL_FLAG)
	{
		snprintf(tg, sizeof(tg), "PRIVATE CALL");
	}
	else
	{
		snprintf(tg, sizeof(tg), "TG %u", (unsigned)(id & 0xFFFFFF));
	}
	mmss((int)((ticksGetMillis() - rxSinceMs) / 1000U), buf, sizeof(buf));
	drawBottomBar(ST_RX, tg, buf);
}

// TX: inverse band with the timer, power and mode; the time-out bar; tone or talk group
static void drawTx(const emcomMainInfo_t *info)
{
	char buf[28];
	char f[16];
	uint8_t powerLevel = trxGetPowerLevel();
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	int t = ((info->txTimeSecs < 0) ? 0 : info->txTimeSecs);
	int remaining = ((currentChannelData->tot != 0) ? t : -1);
	int total = (int)(currentChannelData->tot * 15U);
	int elapsed = ((remaining >= 0) ? (total - t) : t);

	box(0, 0, W, 30, true);
	txt(2, 3, "TX", FONT_SIZE_3, true, AL_L);
	mmss(elapsed, buf, sizeof(buf));
	txt(24, 3, buf, FONT_SIZE_3, true, AL_L);
	snprintf(buf, sizeof(buf), "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
	txt((W - 2), 3, buf, FONT_SIZE_1_BOLD, true, AL_R);
	txt((W - 2), 13, (digital ? "DMR" : (trxGetBandwidthIs25kHz() ? "FM" : "FMN")), FONT_SIZE_1, true, AL_R);

	uint32_t txFreq = ((uiDataGlobal.talkaround || uiDataGlobal.reverseRepeaterChannel) ? currentChannelData->rxFreq : currentChannelData->txFreq);
	formatFreq(txFreq, f, sizeof(f));
	if (info->isVFO)
	{
		snprintf(buf, sizeof(buf), "VFO %c  %s", info->vfoLetter, f);
	}
	else
	{
		char name[20];

		codeplugUtilConvertBufToString(currentChannelData->name, name, 16);
		name[10] = 0;
		snprintf(buf, sizeof(buf), "%-10s %s", name, f);
	}
	txt(2, 21, buf, FONT_SIZE_1, true, AL_L);

	// time-out timer
	txt(0, 33, "TOT", FONT_SIZE_1, false, AL_L);
	if (remaining < 0)
	{
		txt(22, 33, "off", FONT_SIZE_1, false, AL_L);
	}
	else
	{
		levelBar(22, 33, (W - 22), 7, ((total > 0) ? ((remaining * 1000) / total) : 0), 0);
		mmss(remaining, f, sizeof(f));
		snprintf(buf, sizeof(buf), "%s left", f);
		if (remaining <= 10)
		{
			int16_t w = (int16_t)((strlen(buf) * 6) + 3);

			box(0, 43, w, 9, true);
			txt(1, 44, buf, FONT_SIZE_1_BOLD, true, AL_L);
		}
		else
		{
			txt(0, 44, buf, FONT_SIZE_1, false, AL_L);
		}
	}

	if (digital)
	{
		uint32_t id;
		char name[24];

		contactName(name, sizeof(name), &id);
		snprintf(buf, sizeof(buf), "%s%u", (((id >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), (unsigned)(id & 0xFFFFFF));
		txt((W - 1), 44, buf, FONT_SIZE_1, false, AL_R);
		name[20] = 0;
		drawBottomBar(ST_TX, name, NULL);
	}
	else
	{
		if (currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE)
		{
			char tn[10];

			toneString(currentChannelData->txTone, tn, sizeof(tn));
			snprintf(buf, sizeof(buf), "T%s", tn);
			txt((W - 1), 44, buf, FONT_SIZE_1, false, AL_R);
		}
		drawBottomBar(ST_TX, "SK2 1750  0-9 DTMF", "");
	}
}

void emcomRenderMain(const emcomMainInfo_t *info)
{
	emcomState_t st = currentState();
	bool caller = false;
	bool details = false;

	if (info->isTransmitting)
	{
		st = ST_TX;
	}
	if (info != &lastInfo)
	{
		lastInfo = *info;
	}
	lastInfoValid = true;
	inRender = true;
	barVisible = false;

	if ((st == ST_RX) && (drawn.state != ST_RX))
	{
		rxSinceMs = ticksGetMillis();
	}

	displayClearBuf();

	if (st == ST_TX)
	{
		drawTx(info);
	}
	else
	{
		drawStatusBar(st);
		caller = dmrCallerActive();
		details = info->showDetails;

		if (caller && (details == false))
		{
			drawDmrCaller();
		}
		else
		{
			drawMain(info, st);
			if (details)
			{
				drawDetails();
			}
		}
	}

	displayRender();
	inRender = false;

	drawn.valid = true;
	drawn.state = (uint8_t)st;
	drawn.radioMode = (uint8_t)trxGetMode();
	drawn.isVFO = info->isVFO;
	drawn.zoneIndex = currentZone.NOT_IN_CODEPLUGDATA_indexNumber;
	drawn.channelIndex = (info->isVFO ? -1 : codeplugGetLastUsedChannelInCurrentZone());
	drawn.rxFreq = currentChannelData->rxFreq;
	drawn.txFreq = currentChannelData->txFreq;
	drawn.minute = (uiDataGlobal.dateTimeSecs / 60U);
	drawn.gpsFix = gpsHasFix();
	drawn.sats = gpsSats();
	drawn.dmrCaller = caller;
	drawn.details = details;
	drawn.sosPhase = (((ticksGetMillis() / 600U) & 1U) == 0U);
	drawn.locks = (keypadLocked || PTTLocked);
	drawn.lastFull = ticksGetMillis();
	drawn.rxDbm = (int16_t)trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	drawn.rxSecs = ((ticksGetMillis() - rxSinceMs) / 1000U);
#if defined(HAS_DMR_SMS)
	drawn.smsUnread = dmrSmsUnreadCount();
#endif
}

bool emcomNeedsFullRedraw(uint32_t now)
{
	emcomState_t st = currentState();
	bool isVFO = (menuSystemGetRootMenuNumber() == UI_VFO_MODE);

	if (drawn.valid == false)
	{
		return true;
	}
	if (((uint8_t)st != drawn.state) || ((uint8_t)trxGetMode() != drawn.radioMode) || (isVFO != drawn.isVFO) ||
			((uiDataGlobal.dateTimeSecs / 60U) != drawn.minute) || (gpsHasFix() != drawn.gpsFix) || (gpsSats() != drawn.sats) ||
			(dmrCallerActive() != drawn.dmrCaller) || (uiDataGlobal.displayChannelSettings != drawn.details) ||
			((keypadLocked || PTTLocked) != drawn.locks))
	{
		return true;
	}
	if ((currentChannelData == NULL) || (currentChannelData->rxFreq != drawn.rxFreq) || (currentChannelData->txFreq != drawn.txFreq) ||
			((isVFO == false) && ((currentZone.NOT_IN_CODEPLUGDATA_indexNumber != drawn.zoneIndex) ||
					(codeplugGetLastUsedChannelInCurrentZone() != drawn.channelIndex))))
	{
		return true;
	}
#if defined(HAS_DMR_SMS)
	if (dmrSmsUnreadCount() != drawn.smsUnread)
	{
		return true;
	}
#endif
	if (sosOn && ((((now / 600U) & 1U) == 0U) != drawn.sosPhase))
	{
		return true;
	}
	// FM RX: the time on the channel and the level, at most twice per second
	if ((st == ST_RX) && ((now - drawn.lastFull) >= 500U) &&
			((abs(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY) - drawn.rxDbm) >= 2) || (((now - rxSinceMs) / 1000U) != drawn.rxSecs)))
	{
		return true;
	}
	// low supply: the voltage blinks
	if (batteryIsLowWarning() && ((now - drawn.lastFull) >= 500U))
	{
		return true;
	}
	return false;
}

// --- SP7TEAM emblem: antenna mast with radio waves (boot and power-off screens) ---------
void emcomDrawEmblem(int16_t cx, int16_t ty, themeItem_t waves, themeItem_t mast, themeItem_t bg)
{
	UNUSED_PARAMETER(waves);
	UNUSED_PARAMETER(mast);
	UNUSED_PARAMETER(bg);

	for (int16_t r = 7; r <= 19; r += 6)
	{
		for (int16_t a = -40; a <= 40; a += 2)
		{
			double rad = ((double)a * (M_PI / 180.0));
			int16_t dx = (int16_t)lround(cos(rad) * r);
			int16_t dy = (int16_t)lround(sin(rad) * r);

			displaySetPixel((cx + dx), (ty + dy), true);
			displaySetPixel((cx - dx), (ty + dy), true);
		}
	}
	displayFillCircle(cx, ty, 2, false);
	vline(cx, ty, 26);
	displayDrawLine(cx, (ty + 8), (cx - 7), (ty + 26), false);
	displayDrawLine(cx, (ty + 8), (cx + 7), (ty + 26), false);
	hline((cx - 4), (ty + 17), 9);
	hline((cx - 9), (ty + 26), 19);
}

void emcomThemePreset(bool night, uint16_t *items)
{
	UNUSED_PARAMETER(night);
	UNUSED_PARAMETER(items);
}

// APRS beacon on air: an inverse band over the frequency rows
void emcomDrawTxBanner(const char *str)
{
	box(0, (A_LINE_Y + 1), W, 19, true);
	txt(0, (A_LINE_Y + 2), str, FONT_SIZE_3, true, AL_C);
}

// --- message boxes / notifications ----------------------------------------------------------
#define ALERT_CHARS      20
#define ALERT_MAX_LINES  3

static int alertWrap(const char *message, char lines[ALERT_MAX_LINES][ALERT_CHARS + 1])
{
	int count = 0;
	const char *p = (message ? message : "");

	while (*p && (count < ALERT_MAX_LINES))
	{
		size_t len = 0;
		size_t cut;

		while (*p == ' ')
		{
			p++;
		}
		while (p[len] && (p[len] != '\n'))
		{
			len++;
		}
		cut = len;
		if (len > ALERT_CHARS)
		{
			cut = ALERT_CHARS;
			while ((cut > 0) && (p[cut] != ' '))
			{
				cut--;
			}
			if (cut == 0)
			{
				cut = ALERT_CHARS;
			}
		}
		memcpy(lines[count], p, cut);
		lines[count][cut] = 0;
		count++;
		p += cut;
		if (*p == '\n')
		{
			p++;
		}
	}
	return count;
}

static void upper(char *dst, size_t size, const char *src)
{
	size_t n = 0;

	for (; src[n] && (n < (size - 1)); n++)
	{
		char c = src[n];
		dst[n] = (((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c);
	}
	dst[n] = 0;
}

// Card: double frame, inverse title strip with a sign for the kind ("!" warning, "X" error, "?" question)
void emcomDrawAlert(uiAlertKind_t kind, const char *title, const char *message, const char *keys, bool fullScreen)
{
	static const char *const words[] = { "INFO", "INFO", "WARNING", "ERROR", "CONFIRM" };
	char lines[ALERT_MAX_LINES][ALERT_CHARS + 1];
	char head[24];
	char t[22];
	int count = alertWrap(message, lines);
	int16_t top = (fullScreen ? 0 : 9);
	int16_t bottom = ((fullScreen && keys) ? 54 : H);

	if (title == NULL)
	{
		title = words[((kind <= UI_ALERT_QUESTION) ? kind : UI_ALERT_INFO)];
	}
	upper(t, sizeof(t), title);
	snprintf(head, sizeof(head), "%s%s", ((kind == UI_ALERT_WARNING) ? "! " : ((kind == UI_ALERT_ERROR) ? "X " : ((kind == UI_ALERT_QUESTION) ? "? " : ""))), t);
	head[20] = 0;

	box(0, top, W, (H - top), false);
	frame(2, (top + 2), (W - 4), (bottom - top - 4));
	frame(3, (top + 3), (W - 6), (bottom - top - 6));
	box(4, (top + 4), (W - 8), 11, true);
	txt(0, (top + 6), head, FONT_SIZE_1_BOLD, true, AL_C);

	int16_t bodyTop = (top + 16);
	int16_t bodyH = (int16_t)((bottom - 4) - bodyTop);

	if ((count == 1) && (strlen(lines[0]) <= 15))
	{
		// one short value: the large font
		txt(0, (int16_t)(bodyTop + ((bodyH - 16) / 2)), lines[0], FONT_SIZE_3, false, AL_C);
	}
	else
	{
		int16_t ly = (int16_t)(bodyTop + ((bodyH - (count * 9)) / 2));

		for (int i = 0; i < count; i++)
		{
			txt(0, ly, lines[i], FONT_SIZE_1, false, AL_C);
			ly += 9;
		}
	}

	if (fullScreen && keys)
	{
		dotted(0, 54, W);
		txt(0, 56, keys, FONT_SIZE_1, false, AL_L);
	}
}

// Notification with a value and an optional level bar (squelch, volume, power): an inverse strip at the bottom
void emcomDrawLevelAlert(const char *title, const char *value, int percent)
{
	char t[22];

	upper(t, sizeof(t), title);
	box(0, 44, W, 20, false);
	hline(0, 44, W);
	box(0, 46, W, 18, true);
	txt(2, 47, t, FONT_SIZE_1_BOLD, true, AL_L);
	txt((W - 3), 47, value, FONT_SIZE_1_BOLD, true, AL_R);
	if (percent >= 0)
	{
		// light frame and fill on the dark strip
		int16_t x = 2, y = 56, w = (W - 4), h = 6;
		int16_t fw = (int16_t)(((w - 2) * CLAMP(percent, 0, 100)) / 100);

		box(x, y, w, 1, false);
		box(x, (y + h - 1), w, 1, false);
		box(x, y, 1, h, false);
		box((x + w - 1), y, 1, h, false);
		box((x + 1), (y + 1), fw, (h - 2), false);
	}
}

// --- tool screens (128 x 64): title, chips, key bar -------------------------------------------
bool emcomUiLarge(void)
{
	return false;
}

int16_t emcomUiText(int16_t x, int16_t y, const char *str, ucFont_t font, themeItem_t colour, bool alignRight)
{
	UNUSED_PARAMETER(colour);
	return txt(x, y, str, font, false, (alignRight ? AL_R : AL_L));
}

int16_t emcomUiChip(int16_t x, int16_t y, const char *str, themeItem_t colour, emcomChip_t style, bool alignRight)
{
	int16_t w = (int16_t)((strlen(str) * 6) + 3);

	UNUSED_PARAMETER(colour);
	if (alignRight)
	{
		x -= w;
	}
	if (style == EMCOM_CHIP_SOLID)
	{
		box(x, (y - 1), w, 9, true);
		txt((x + 2), y, str, FONT_SIZE_1_BOLD, true, AL_L);
	}
	else
	{
		frame(x, (y - 1), w, 9);
		txt((x + 2), y, str, FONT_SIZE_1, false, AL_L);
	}
	return w;
}

int16_t emcomUiKeyValue(int16_t x, int16_t y, const char *key, const char *value, themeItem_t colour)
{
	int16_t w = (int16_t)(txt(x, y, key, FONT_SIZE_1, false, AL_L) + 4);

	UNUSED_PARAMETER(colour);
	return (int16_t)(w + txt((x + w), y, value, FONT_SIZE_1_BOLD, false, AL_L));
}

void emcomUiTitle(const char *title, const char *counter)
{
	char t[22];

	upper(t, sizeof(t), title);
	box(0, 0, W, HEADER_H, true);
	int16_t rw = (counter ? (int16_t)(txt((W - 2), 1, counter, FONT_SIZE_1, true, AL_R) + 6) : 0);
	size_t maxChars = (size_t)((W - 2 - rw) / 6);
	if (strlen(t) > maxChars)
	{
		t[maxChars] = 0;
	}
	txt(1, 1, t, FONT_SIZE_1_BOLD, true, AL_L);
}

void emcomUiStrip(void)
{
	box(0, EMCOM_UI_STRIP_Y, W, EMCOM_UI_STRIP_H, false);
}

void emcomUiDivider(int16_t y)
{
	dotted(0, y, W);
}

// The key hints of the shared screens say RED for the back key: on the MD-9600 that is A on the
// microphone (or BACK on the front panel), so "RED" is shown as "A"
static void micKeys(char *dst, size_t size, const char *src)
{
	size_t o = 0;

	for (size_t i = 0; src[i] && (o < (size - 1)); i++)
	{
		if ((strncmp(&src[i], "RED", 3) == 0) && ((i == 0) || (src[i - 1] == ' ')) && ((src[i + 3] == ' ') || (src[i + 3] == 0)))
		{
			dst[o++] = 'A';
			i += 2;
			continue;
		}
		dst[o++] = src[i];
	}
	dst[o] = 0;
}

void emcomUiKeyBar(const char *keys)
{
	char buf[32];

	micKeys(buf, sizeof(buf), keys);
	box(0, (EMCOM_UI_KEYBAR_Y - 1), W, (H - EMCOM_UI_KEYBAR_Y + 1), false);
	dotted(0, (EMCOM_UI_KEYBAR_Y - 1), W);
	txt(0, EMCOM_UI_KEYBAR_Y, buf, FONT_SIZE_1, false, AL_L);
}

void emcomUiConfirmBar(const char *question)
{
	int16_t x;

	emcomUiKeyBar("");
	x = (int16_t)(emcomUiChip(0, EMCOM_UI_KEYBAR_Y, question, THEME_ITEM_FG_DEFAULT, EMCOM_CHIP_SOLID, false) + 3);
	txt(x, EMCOM_UI_KEYBAR_Y, "GRN YES ANY NO", FONT_SIZE_1, false, AL_L);
}

// --- SOS (same behaviour as uiEmcom.c) -------------------------------------------------------
bool emcomSosActive(void)
{
	return sosOn;
}

void emcomSosRequest(void)
{
	static uint32_t pendingSince = 0U;
	uint32_t now = ticksGetMillis();

	if ((pendingSince != 0U) && ((now - pendingSince) <= 3000U))
	{
		pendingSince = 0U;
		emcomSosToggle();
		return;
	}

	pendingSince = now;
	uiNotificationShowKind(UI_ALERT_QUESTION, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 3000U, (sosOn ? "Again: SOS OFF" : "Again: SOS ON"), false);
	soundSetMelody(MELODY_KEY_BEEP);
}

void emcomSosToggle(void)
{
	sosOn = !sosOn;
	sosSent = 0U;
	sosNextMs = ticksGetMillis();
	sosPauseShown = false;
	drawn.valid = false;

	uiNotificationShowKind((sosOn ? UI_ALERT_WARNING : UI_ALERT_INFO), NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2000U, (sosOn ? "SOS: ON" : "SOS: OFF"), false);
#if defined(HAS_EVENT_LOG)
	eventLogAdd(EVENT_LOG_SOS, "%s", (sosOn ? "SOS ON" : "SOS OFF"));
#endif
	soundSetMelody(sosOn ? MELODY_ERROR_BEEP : MELODY_ACK_BEEP);
}

static void sosSend(void)
{
	char info[64];
	double lat, lon;

	if (ownPosition(&lat, &lon))
	{
		double alat = fabs(lat), alon = fabs(lon);
		uint32_t latDeg = (uint32_t)alat, lonDeg = (uint32_t)alon;
		uint32_t latMin = (uint32_t)(((alat - latDeg) * 6000.0) + 0.5);
		uint32_t lonMin = (uint32_t)(((alon - lonDeg) * 6000.0) + 0.5);

		if (latMin >= 6000U)
		{
			latMin -= 6000U;
			latDeg++;
		}
		if (lonMin >= 6000U)
		{
			lonMin -= 6000U;
			lonDeg++;
		}
		snprintf(info, sizeof(info), "!%02u%02u.%02u%c\\%03u%02u.%02u%c!EMERGENCY SOS",
				(unsigned)latDeg, (unsigned)(latMin / 100U), (unsigned)(latMin % 100U), ((lat >= 0.0) ? 'N' : 'S'),
				(unsigned)lonDeg, (unsigned)(lonMin / 100U), (unsigned)(lonMin % 100U), ((lon >= 0.0) ? 'E' : 'W'));
	}
	else
	{
		snprintf(info, sizeof(info), ">EMERGENCY SOS (no position)");
	}

	aprsInfoTxResult_t res = aprsSendInfoFrame(info);

	switch (res)
	{
		case APRS_INFO_TX_SENT:
			sosSent++;
#if defined(HAS_EVENT_LOG)
			eventLogAdd(EVENT_LOG_SOS, "beacon %u", (unsigned)sosSent);
#endif
			sosNextMs = (ticksGetMillis() + ((sosSent < SOS_FAST_BEACONS) ? SOS_FIRST_INTERVAL_MS : SOS_INTERVAL_MS));
			break;
		case APRS_INFO_TX_NO_CONFIG:
		case APRS_INFO_TX_NOT_ALLOWED:
			uiNotificationShowKind(UI_ALERT_ERROR, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2500U,
					((res == APRS_INFO_TX_NO_CONFIG) ? "SOS: no APRS config" : "SOS: TX not allowed"), true);
			sosNextMs = (ticksGetMillis() + SOS_INTERVAL_MS);
			break;
		case APRS_INFO_TX_CHANNEL_BUSY:
			sosNextMs = (ticksGetMillis() + SOS_BUSY_RETRY_MS);
			break;
		default:
			sosNextMs = (ticksGetMillis() + 5000U);
			break;
	}
}

static bool sosScreenAllowsTx(int menu)
{
	switch (menu)
	{
		case UI_CHANNEL_MODE:
		case UI_VFO_MODE:
		case UI_LOCK_SCREEN:
		case MENU_LAST_HEARD:
#if defined(HAS_GPS)
		case MENU_GPS:
#endif
#if defined(HAS_FIELD_TOOLS_MENU)
		case MENU_POSITION:
		case MENU_PACE:
		case MENU_COVERAGE:
		case MENU_RADIO_CHECK:
		case MENU_NET_CONTROL:
		case MENU_WAYPOINTS:
		case MENU_SKED:
		case MENU_EVENT_LOG:
#endif
			return true;
		default:
			return false;
	}
}

void emcomSosTick(void)
{
	int menu = menuSystemGetCurrentMenuNumber();

	if ((sosOn == false) || ((int32_t)(ticksGetMillis() - sosNextMs) < 0))
	{
		return;
	}
	if (trxTransmissionEnabled || aprsBeaconingIsTransmitting())
	{
		return;
	}
	if (sosScreenAllowsTx(menu) == false)
	{
		if ((sosPauseShown == false) && (menu != UI_CPS) && (menu != UI_HOTSPOT_MODE) && (menu != UI_POWER_OFF) &&
				(menu != UI_SPLASH_SCREEN) && (menu != MENU_CALIBRATION) && (menu != UI_TX_SCREEN))
		{
			sosPauseShown = true;
			uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2000U, "SOS PAUSED", false);
		}
		return;
	}
	sosPauseShown = false;
	sosSend();
}

#endif // HAS_EMCOM_UI && EMCOM_UI_MONO
