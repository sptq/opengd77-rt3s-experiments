/*
 * Fork: "EMCOM" main screen style, by Marcin SP7TQ. See uiEmcom.h.
 *
 * Layout (160 x 128, y in pixels):
 *   0..10   status bar: state chip (mode / RX / TX / SCAN), power, tone or
 *           time slot, APRS, GPS, volume, battery; SOS banner
 *   12..14  S-meter / mic level, 32 segments
 *   17      zone (or VFO) | CH n/m
 *   20..47  channel number (16x32) + name (8x16) + frequency, or VFO frequency
 *   50      divider
 *   54..73  panel: parameters, or RX level, DMR caller, TX timer
 *   75      divider
 *   79..106 EMCOM rows: APRS stations / messages / locator, position, last heard
 *   116..127 footer: UTC clock, lock / scan / SOS
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiEmcom.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/codeplug.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "functions/aprs.h"
#include "interfaces/batteryAndPowerManagement.h"
#include "interfaces/i2s.h"
#include "io/LEDs.h"
#include "io/keyboard.h"
#include "io/buttons.h"
#include "hardware/HR-C6000.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif
#if defined(I2S_APRS_RX)
#include "functions/aprsRx.h"
#endif

#if defined(HAS_EMCOM_UI)

extern int8_t lastVolume;

// --- palette ------------------------------------------------------------------------
typedef struct
{
	uint32_t bg, fg, mut, line, bar, barFg, rx, tx, scan, info, seg;
} emcomPalette_t;

static const emcomPalette_t PALETTE_DARK =
{
	.bg = 0x0B0E0C, .fg = 0xF2F5F0, .mut = 0x86928A, .line = 0x28302B, .bar = 0x18201B, .barFg = 0xE9EEE7,
	.rx = 0x3BD16F, .tx = 0xFF3B30, .scan = 0xFFB000, .info = 0x5AC8FA, .seg = 0x202823
};
// Night: red only (keeps the night vision, low light signature)
static const emcomPalette_t PALETTE_NIGHT =
{
	.bg = 0x000000, .fg = 0xD22A2A, .mut = 0x7A1414, .line = 0x3A0808, .bar = 0x120202, .barFg = 0xD22A2A,
	.rx = 0xE23A3A, .tx = 0xFF4A4A, .scan = 0xA51C1C, .info = 0xC02626, .seg = 0x250505
};

#define STATUS_H        11
#define METER_Y         12
#define METER_H         3
#define ZONE_Y          17
#define HERO_Y          20   // 16x32 cell, glyphs in rows 6..24
#define NAME_Y          24   // 8x16 cell, glyphs in rows 2..12
#define FREQ_Y          40
#define DIV1_Y          50
#define PANEL_Y1        54
#define PANEL_Y2        64
#define DIV2_Y          75
#define EMCOM_Y1        79
#define EMCOM_Y2        89
#define EMCOM_Y3        99
#define FOOTER_Y        (DISPLAY_SIZE_Y - 12) // 116 on 128-line screens, 108 on the DM-1701
#define S9_SEGMENT      20
#define COL2_X          66   // parameter columns
#define COL3_X          120

typedef enum
{
	ST_STBY = 0,
	ST_RX,
	ST_TX,
	ST_SCAN
} emcomState_t;

static const emcomPalette_t *pal = &PALETTE_DARK;
static bool vfoAllowed = true;
static bool channelAllowed = true;

// last drawn state (for emcomNeedsFullRedraw)
static struct
{
	uint8_t  state;
	int8_t   volume;
	uint32_t minute;
	uint32_t unread;
	uint32_t stations;
	bool     gpsFix;
	bool     sosPhase;
	int16_t  rxDbm;
	uint32_t lastFull;
	bool     valid;
} drawn;

// SOS
#define SOS_FIRST_INTERVAL_MS   60000U
#define SOS_INTERVAL_MS         120000U
#define SOS_FAST_BEACONS        3U
#define SOS_BUSY_RETRY_MS       20000U
static bool sosOn = false;
static uint32_t sosNextMs;
static uint32_t sosSent;

// --- drawing helpers ------------------------------------------------------------------
static void colour(uint32_t rgb)
{
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(rgb), displayConvertRGB888ToNative(pal->bg));
}

static void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rgb)
{
	colour(rgb);
	displayFillRect(x, y, w, h, false);
}

static int16_t fontWidth(ucFont_t font)
{
	return ((font == FONT_SIZE_4) ? 16 : (((font == FONT_SIZE_3) || (font == FONT_SIZE_2)) ? 8 : 6));
}

// Draws 'str' (clipped to the screen), returns its width. alignRight: x is the right edge.
static int16_t text(int16_t x, int16_t y, const char *str, ucFont_t font, uint32_t rgb, bool alignRight)
{
	int16_t w = (int16_t)(strlen(str) * fontWidth(font));

	if (alignRight)
	{
		x -= w;
	}
	if (x < 0)
	{
		x = 0;
	}
	colour(rgb);
	displayPrintCore(x, y, str, font, TEXT_ALIGN_LEFT, false);
	return w;
}

// "KEY value": key muted, value in 'rgb'; returns the total width
static int16_t keyValue(int16_t x, int16_t y, const char *key, const char *value, uint32_t rgb)
{
	int16_t w = text(x, y, key, FONT_SIZE_1, pal->mut, false) + 4;
	return (w + text((x + w), y, value, FONT_SIZE_1, rgb, false));
}

static void divider(int16_t y)
{
	fill(0, y, DISPLAY_SIZE_X, 1, pal->line);
}

static void selectPalette(void)
{
	pal = ((DAYTIME_CURRENT == NIGHT) ? &PALETTE_NIGHT : &PALETTE_DARK);
}

// --- state ----------------------------------------------------------------------------
bool emcomStyleEnabled(void)
{
	return (settingsIsOptionBitSet(BIT_UI_CLASSIC_STYLE) == false);
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

	// TX screen showing the last heard list, or transmitting from the satellite screen
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

static bool ownPosition(double *lat, double *lon)
{
#if defined(I2S_APRS_RX)
	return aprsRxOwnPosition(lat, lon);
#else
	if (settingsLocationIsValid())
	{
		*lat = settingsLocationGetLatitude();
		*lon = settingsLocationGetLongitude();
		return true;
	}
	return false;
#endif
}

static bool gpsHasFix(void)
{
#if defined(HAS_GPS)
	return ((SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON) && ((gpsData.Status & GPS_STATUS_HAS_FIX) != 0));
#else
	return false;
#endif
}

static int volumeLevel(void)
{
#if defined(HAS_SOFT_VOLUME)
	return uiUtilityVolumeLevel();
#else
	return 4;
#endif
}

// --- status bar -------------------------------------------------------------------------
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

void emcomRenderStatusBar(void)
{
	char buf[16];
	emcomState_t st = currentState();
	bool isTx = (st == ST_TX);
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	uint32_t barCol = (isTx ? pal->tx : pal->bar);
	uint32_t fg = (isTx ? 0xFFFFFF : pal->barFg);
	int16_t x;

	selectPalette();

	fill(0, 0, DISPLAY_SIZE_X, STATUS_H, barCol);
	fill(0, STATUS_H, DISPLAY_SIZE_X, 1, pal->bg); // the classic header clear uses the theme colour

	if (sosOn && (st != ST_TX))
	{
		bool phase = (((ticksGetMillis() / 600U) & 1U) == 0U);

		fill(0, 0, DISPLAY_SIZE_X, STATUS_H, (phase ? pal->tx : pal->bar));
		text(((DISPLAY_SIZE_X - (13 * 6)) / 2), 2, "EMERGENCY SOS", FONT_SIZE_1_BOLD, (phase ? 0xFFFFFF : pal->tx), false);
		displayThemeResetToDefault();
		return;
	}

	// State chip: the mode when idle, else the state
	const char *mode = (digital ? "DMR" : (trxGetBandwidthIs25kHz() ? "FM" : "FMN"));
	const char *chip = ((st == ST_RX) ? "RX" : ((st == ST_TX) ? "TX" : ((st == ST_SCAN) ? "SCAN" : mode)));
	uint32_t chipCol = ((st == ST_RX) ? pal->rx : ((st == ST_TX) ? 0xFFFFFF : ((st == ST_SCAN) ? pal->scan : pal->barFg)));
	int16_t chipW = (int16_t)((strlen(chip) * 6) + 4);

	fill(1, 1, chipW, 9, chipCol);
	text(3, 2, chip, FONT_SIZE_1_BOLD, (isTx ? pal->tx : pal->bar), false);
	x = chipW + 4;

	if (st != ST_STBY)
	{
		x += text(x, 2, mode, FONT_SIZE_1, fg, false) + 4;
	}

	uint8_t powerLevel = trxGetPowerLevel();
	snprintf(buf, sizeof(buf), "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
	x += text(x, 2, buf, FONT_SIZE_1, fg, false) + 4;

	if (digital)
	{
		snprintf(buf, sizeof(buf), "TS%d", (trxGetDMRTimeSlot() + 1));
		x += text(x, 2, buf, FONT_SIZE_1, fg, false) + 4;
	}
	else if ((currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE) || (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE))
	{
		uint16_t t = ((currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE) ? currentChannelData->txTone : currentChannelData->rxTone);
		snprintf(buf, sizeof(buf), "%s%s", ((codeplugGetCSSType(t) & CSS_TYPE_DCS) ? "DT" : "CT"), ((currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE) ? "R" : ""));
		x += text(x, 2, buf, FONT_SIZE_1, fg, false) + 4;
	}

	if (currentChannelData->aprsConfigIndex != 0)
	{
		bool beaconing = ((aprsBeaconingGetMode() != APRS_BEACONING_MODE_OFF) && (aprsBeaconingIsSuspended() == false));
		text(x, 2, "a", (beaconing ? FONT_SIZE_1_BOLD : FONT_SIZE_1), (isTx ? fg : (beaconing ? pal->rx : pal->mut)), false);
	}

	// Right side: battery icon + %, volume, GPS
	int16_t r = (DISPLAY_SIZE_X - 2);
	int pct = getBatteryPercentage();
	bool low = batteryIsLowWarning();
	uint32_t battCol = (low ? pal->tx : fg);

	colour(battCol);
	displayDrawRect((r - 13), 2, 12, 7, false);
	displayFillRect((r - 1), 4, 1, 3, false);
	displayFillRect((r - 12), 3, (int16_t)CLAMP(((pct * 10) + 50) / 100, 0, 10), 5, false);
	r -= 16;
	snprintf(buf, sizeof(buf), "%d%%", pct);
	r -= text(r, 2, buf, FONT_SIZE_1, battCol, true) + 4;

	int vol = volumeLevel();
	if (vol < 0)
	{
		colour(fg);
		displayDrawLine((r - 7), 3, (r - 2), 8, false);
		displayDrawLine((r - 7), 8, (r - 2), 3, false);
	}
	else
	{
		for (int b = 0; b < 4; b++)
		{
			int16_t h = (int16_t)(2 * (b + 1));
			int16_t bx = (int16_t)((r - 8) + (b * 2));

			fill(bx, (b < vol) ? (9 - h) : 8, 1, ((b < vol) ? h : 1), fg);
		}
	}
	r -= 12;

#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON)
	{
		text(r, 2, "GPS", FONT_SIZE_1_BOLD, (gpsHasFix() ? (isTx ? fg : pal->rx) : pal->mut), true);
	}
#endif

	displayThemeResetToDefault();
}

// --- S-meter / mic level -------------------------------------------------------------------------
void emcomDrawBar(int barWidth, bool isTx)
{
	int lit = CLAMP((barWidth + 2) / 5, 0, 32);

	selectPalette();
	for (int i = 0; i < 32; i++)
	{
		uint32_t c = pal->seg;

		if (i < lit)
		{
			c = (isTx ? pal->tx : ((i < S9_SEGMENT) ? pal->rx : pal->scan));
		}
		fill((int16_t)(i * 5), METER_Y, 4, METER_H, c);
	}
	fill(0, (METER_Y + METER_H), DISPLAY_SIZE_X, 1, pal->bg);
	displayThemeResetToDefault();
}

static int dbmToSegments(int dBm)
{
	if (dBm <= SMETER_S0)
	{
		return 0;
	}
	if (dBm <= SMETER_S9)
	{
		return (((dBm - SMETER_S0) * S9_SEGMENT) / (SMETER_S9 - SMETER_S0));
	}
	return CLAMP(S9_SEGMENT + ((dBm - SMETER_S9) / 5), 0, 32);
}

void emcomDrawRssi(int dBm)
{
	emcomDrawBar((dbmToSegments(dBm) * 5), false);
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
		int s = ((dBm - SMETER_S0) / 4);
		snprintf(buf, size, "S%d", CLAMP(s, 0, 9));
	}
}

// --- body -----------------------------------------------------------------------------------
static void formatFreq(uint32_t freq, char *buf, size_t size, bool withUnit)
{
	// frequencies are in 10 Hz units: 4 decimals, 5 when the 10 Hz digit is used (6.25 / 2.5 kHz steps)
	uint32_t dec = (freq % 100000U);

	if ((dec % 10U) != 0U)
	{
		snprintf(buf, size, "%u.%05u%s", (unsigned)(freq / 100000U), (unsigned)dec, (withUnit ? " MHz" : ""));
	}
	else
	{
		snprintf(buf, size, "%u.%04u%s", (unsigned)(freq / 100000U), (unsigned)(dec / 10U), (withUnit ? " MHz" : ""));
	}
}

static void drawHero(const emcomMainInfo_t *info, emcomState_t st)
{
	char buf[24];
	bool txFreqShown = (st == ST_TX);
	uint32_t rxFreq = currentChannelData->rxFreq;
	uint32_t txFreq = currentChannelData->txFreq;

	if (info->isVFO)
	{
		uint32_t f = (txFreqShown ? txFreq : rxFreq);

		formatFreq(f, buf, sizeof(buf), false);
		int16_t w = text(1, HERO_Y, buf, FONT_SIZE_4, (txFreqShown ? pal->tx : pal->fg), false);
		if ((w + 1 + 2 + 18) <= DISPLAY_SIZE_X) // 9 digits leave no room for the unit
		{
			text((DISPLAY_SIZE_X - 2), FREQ_Y, "MHz", FONT_SIZE_1, pal->mut, true);
		}
		return;
	}

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

	snprintf(buf, sizeof(buf), "%02d", info->channelNumber);
	int16_t numW = text(1, HERO_Y, buf, FONT_SIZE_4, pal->fg, false);
	int16_t nameX = (int16_t)(numW + 6);

	char name[20];
	codeplugUtilConvertBufToString(currentChannelData->name, name, 16);
	size_t maxChars = (size_t)((DISPLAY_SIZE_X - nameX) / 8);
	if (strlen(name) > maxChars)
	{
		name[maxChars] = 0;
	}
	text(nameX, NAME_Y, name, FONT_SIZE_3, pal->fg, false);

	formatFreq((txFreqShown ? txFreq : rxFreq), buf, sizeof(buf), true);
	size_t l = strlen(buf);
	if (txFreqShown)
	{
		snprintf(&buf[l], (sizeof(buf) - l), " TX");
	}
	else if (uiDataGlobal.reverseRepeaterChannel)
	{
		snprintf(&buf[l], (sizeof(buf) - l), " REV");
	}
	else if (uiDataGlobal.talkaround)
	{
		snprintf(&buf[l], (sizeof(buf) - l), " TA");
	}
	text(nameX, FREQ_Y, buf, FONT_SIZE_1, (txFreqShown ? pal->tx : pal->info), false);
}

static void drawZoneRow(const emcomMainInfo_t *info)
{
	char buf[24];

	if (info->isVFO)
	{
		snprintf(buf, sizeof(buf), "VFO %c", info->vfoLetter);
		text(2, ZONE_Y, buf, FONT_SIZE_1, pal->mut, false);

		int32_t offset = ((int32_t)currentChannelData->txFreq - (int32_t)currentChannelData->rxFreq) / 100; // kHz
		if (offset != 0)
		{
			snprintf(buf, sizeof(buf), "TX %c%d.%03d", ((offset < 0) ? '-' : '+'), (int)(abs(offset) / 1000), (int)(abs(offset) % 1000));
			text((DISPLAY_SIZE_X - 2), ZONE_Y, buf, FONT_SIZE_1, pal->mut, true);
		}
		return;
	}

	char zone[20];
	snprintf(zone, sizeof(zone), "%s", (info->zoneName ? info->zoneName : ""));
	zone[15] = 0;
	text(2, ZONE_Y, zone, FONT_SIZE_1, pal->mut, false);

	if (info->channelCount > 0)
	{
		snprintf(buf, sizeof(buf), "CH %d/%d", info->channelNumber, info->channelCount);
	}
	else
	{
		snprintf(buf, sizeof(buf), "CH %d", info->channelNumber);
	}
	text((DISPLAY_SIZE_X - 2), ZONE_Y, buf, FONT_SIZE_1, pal->mut, true);
}

// "3m" / "45s" (fits the third column; minutes rounded)
static void totString(char *buf, size_t size)
{
	uint32_t secs = (currentChannelData->tot * 15U);

	// at most 3 characters
	if (secs < 60U)
	{
		snprintf(buf, size, "%us", (unsigned)secs);
	}
	else
	{
		snprintf(buf, size, "%um", (unsigned)((secs + 30U) / 60U));
	}
}

static void drawParams(void)
{
	char buf[16];

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));
		char contact[20];

		snprintf(buf, sizeof(buf), "%u", (unsigned)(PCorTG & 0xFFFFFF));
		keyValue(2, PANEL_Y1, (((PCorTG >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), buf, pal->fg);
		snprintf(buf, sizeof(buf), "%u", (unsigned)trxGetDMRColourCode());
		keyValue(COL2_X, PANEL_Y1, "CC", buf, pal->fg);
		snprintf(buf, sizeof(buf), "%d", (trxGetDMRTimeSlot() + 1));
		keyValue(COL3_X, PANEL_Y1, "TS", buf, pal->fg);

		codeplugUtilConvertBufToString(currentContactData.name, contact, 16);
		if (nonVolatileSettings.overrideTG != 0)
		{
			uiUtilityBuildTgOrPCDisplayName(contact, sizeof(contact));
		}
		contact[16] = 0;
		text(2, PANEL_Y2, contact, FONT_SIZE_1, pal->fg, false);
		return;
	}

	int32_t offset = ((int32_t)currentChannelData->txFreq - (int32_t)currentChannelData->rxFreq) / 100; // kHz
	if (offset == 0)
	{
		snprintf(buf, sizeof(buf), "0");
	}
	else
	{
		snprintf(buf, sizeof(buf), "%c%d.%03d", ((offset < 0) ? '-' : '+'), (int)(abs(offset) / 1000), (int)(abs(offset) % 1000));
	}
	keyValue(2, PANEL_Y1, "OFS", buf, pal->fg);

	toneString(currentChannelData->rxTone, buf, sizeof(buf));
	keyValue(COL2_X, PANEL_Y1, "RXT", buf, pal->fg);

	uint8_t sql = ((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql);
	snprintf(buf, sizeof(buf), "%d", (int)((sql > 0) ? (5 * (sql - 1)) : 0));
	keyValue(COL3_X, PANEL_Y1, "SQL", buf, pal->fg);

	keyValue(2, PANEL_Y2, "BW", (trxGetBandwidthIs25kHz() ? "25k" : "12.5k"), pal->fg);

	toneString(currentChannelData->txTone, buf, sizeof(buf));
	keyValue(COL2_X, PANEL_Y2, "TXT", buf, pal->fg);

	if (currentChannelData->tot == 0)
	{
		snprintf(buf, sizeof(buf), "off");
	}
	else
	{
		totString(buf, sizeof(buf));
	}
	keyValue(COL3_X, PANEL_Y2, "TOT", buf, pal->fg);
}

static void drawRxPanel(void)
{
	char buf[16];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

	snprintf(buf, sizeof(buf), "%ddBm", dBm);
	text(2, (PANEL_Y1 - 2), buf, FONT_SIZE_3, pal->rx, false);
	sUnits(dBm, buf, sizeof(buf));
	text((DISPLAY_SIZE_X - 2), (PANEL_Y1 - 2), buf, FONT_SIZE_3, pal->rx, true);

	if (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE)
	{
		toneString(currentChannelData->rxTone, buf, sizeof(buf));
		keyValue(2, (PANEL_Y2 + 2), "RXT", buf, pal->rx);
	}
	else
	{
		text(2, (PANEL_Y2 + 2), "CARRIER", FONT_SIZE_1, pal->mut, false);
	}
}

static void drawCallerPanel(void)
{
	char buf[40];
	char call[SCREEN_LINE_BUFFER_SIZE]; // getCallsignOnly() writes up to SCREEN_LINE_BUFFER_SIZE bytes

	if ((LinkHead == NULL) || (HRC6000GetReceivedTgOrPcId() == 0))
	{
		drawParams();
		return;
	}

	bool useTA = ((LinkHead->talkerAlias[0] != 0) &&
			((nonVolatileSettings.contactDisplayPriority == CONTACT_DISPLAY_PRIO_TA_CC_DB) ||
					(nonVolatileSettings.contactDisplayPriority == CONTACT_DISPLAY_PRIO_TA_DB_CC) ||
					(strncmp(LinkHead->contact, "ID:", 3) == 0)));
	const char *src = (useTA ? LinkHead->talkerAlias : LinkHead->contact);

	getCallsignOnly(call, src);
	call[12] = 0;
	text(2, (PANEL_Y1 - 2), call, FONT_SIZE_3, pal->rx, false);

	if ((LinkHead->talkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		text((DISPLAY_SIZE_X - 2), PANEL_Y1, "PRIVATE", FONT_SIZE_1_BOLD, pal->scan, true);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)(LinkHead->talkGroupOrPcId & 0xFFFFFF));
		int16_t w = (int16_t)((strlen(buf) + 3) * 6);
		keyValue((DISPLAY_SIZE_X - 2 - w), PANEL_Y1, "TG", buf, pal->fg);
	}

	// rest of the contact / TA text (name, place) on the second line
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
	buf[26] = 0;
	text(2, (PANEL_Y2 + 2), buf, FONT_SIZE_1, pal->fg, false);
}

static void drawTxPanel(const emcomMainInfo_t *info)
{
	char buf[16];
	int t = ((info->txTimeSecs < 0) ? 0 : info->txTimeSecs);

	snprintf(buf, sizeof(buf), "%d:%02d", (t / 60), (t % 60));
	text(1, (PANEL_Y1 - 10), buf, FONT_SIZE_4, pal->tx, false);

	if (currentChannelData->tot != 0)
	{
		totString(buf, sizeof(buf));
		keyValue(84, PANEL_Y1, "TOT", buf, pal->fg); // the timer counts down
	}
	else
	{
		keyValue(84, PANEL_Y1, "TOT", "off", pal->mut);
	}

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));
		snprintf(buf, sizeof(buf), "%u", (unsigned)(PCorTG & 0xFFFFFF));
		keyValue(84, PANEL_Y2, (((PCorTG >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), buf, pal->fg);
	}
	else if (currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE)
	{
		toneString(currentChannelData->txTone, buf, sizeof(buf));
		keyValue(84, PANEL_Y2, "TXT", buf, pal->fg);
	}
}

static void formatPosition(double lat, double lon, char *buf, size_t size)
{
	uint32_t la = (uint32_t)((fabs(lat) * 10000.0) + 0.5);
	uint32_t lo = (uint32_t)((fabs(lon) * 10000.0) + 0.5);

	snprintf(buf, size, "%u.%04u%c %u.%04u%c", (unsigned)(la / 10000U), (unsigned)(la % 10000U), ((lat >= 0.0) ? 'N' : 'S'),
			(unsigned)(lo / 10000U), (unsigned)(lo % 10000U), ((lon >= 0.0) ? 'E' : 'W'));
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

static void drawEmcomRows(void)
{
	char buf[28];
	double lat, lon;
	bool hasPos = ownPosition(&lat, &lon);
	uint32_t now = ticksGetMillis();

#if defined(I2S_APRS_RX)
	uint32_t unread = aprsRxMessageUnreadCount();

	if (aprsRxGetBackground())
	{
		snprintf(buf, sizeof(buf), "%u stn", (unsigned)aprsRxStationCount());
		keyValue(2, EMCOM_Y1, "APRS", buf, pal->fg);
	}
	else
	{
		keyValue(2, EMCOM_Y1, "APRS", "off", pal->mut);
	}

	if (unread)
	{
		snprintf(buf, sizeof(buf), "MSG %u", (unsigned)unread);
		fill(72, (EMCOM_Y1 - 1), (int16_t)((strlen(buf) * 6) + 4), 10, pal->info);
		text(74, EMCOM_Y1, buf, FONT_SIZE_1_BOLD, pal->bg, false);
	}
	else
	{
		keyValue(72, EMCOM_Y1, "MSG", "0", pal->mut);
	}
#endif

	if (hasPos)
	{
		char loc[10];

		memset(loc, 0, sizeof(loc));
		coordsToMaidenhead((uint8_t *)loc, lat, lon);
		loc[6] = 0;
		text((DISPLAY_SIZE_X - 2), EMCOM_Y1, loc, FONT_SIZE_1, (gpsHasFix() ? pal->rx : pal->fg), true);

		formatPosition(lat, lon, buf, sizeof(buf));
		keyValue(2, EMCOM_Y2, "POS", buf, pal->fg);
	}
	else
	{
		keyValue(2, EMCOM_Y2, "POS", "no fix / location", pal->mut);
	}

	// Last heard: DMR station, else the last APRS station
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (uiDataGlobal.lastHeardCount > 0) && (LinkHead != NULL))
	{
		char call[SCREEN_LINE_BUFFER_SIZE]; // getCallsignOnly() writes up to SCREEN_LINE_BUFFER_SIZE bytes
		char age[8];

		getCallsignOnly(call, (((LinkHead->talkerAlias[0] != 0) && (strncmp(LinkHead->contact, "ID:", 3) == 0)) ? LinkHead->talkerAlias : LinkHead->contact));
		call[12] = 0;
		ageString((now - LinkHead->time), age, sizeof(age));
		snprintf(buf, sizeof(buf), "%s %s", call, age);
		keyValue(2, EMCOM_Y3, "LH", buf, pal->fg);
	}
#if defined(I2S_APRS_RX)
	else if (aprsRxStationCount() > 0)
	{
		const aprsRxStation_t *st = aprsRxStationGet(0);
		char age[8];

		ageString((now - st->lastMs), age, sizeof(age));
		if (st->hasPos && hasPos)
		{
			char dist[12];
			aprsRxFormatDistance(aprsRxDistanceKm(lat, lon, st->lat, st->lon), dist, sizeof(dist));
			snprintf(buf, sizeof(buf), "%s %s %s", st->name, dist, age);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%s %s", st->name, age);
		}
		keyValue(2, EMCOM_Y3, "LH", buf, pal->fg);
	}
#endif
}

static void drawFooter(emcomState_t st)
{
	char buf[24];
	const char *right = NULL;
	uint32_t rightCol = pal->barFg;

	fill(0, FOOTER_Y, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - FOOTER_Y), pal->bar);

	if (uiDataGlobal.dateTimeSecs > 86400U)
	{
		struct tm t;
		time_t_custom secs = uiDataGlobal.dateTimeSecs;
		static const char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

		gmtime_r_Custom(&secs, &t);
		snprintf(buf, sizeof(buf), "%02d:%02dZ %02d%s", t.tm_hour, t.tm_min, t.tm_mday, months[CLAMP(t.tm_mon, 0, 11)]);
	}
	else
	{
		snprintf(buf, sizeof(buf), "--:--Z");
	}

	if (st == ST_SCAN)
	{
		snprintf(buf, sizeof(buf), "ANY KEY: STOP");
	}
	text(2, (FOOTER_Y + 2), buf, FONT_SIZE_1, pal->barFg, false);

	if (sosOn)
	{
		right = "SOS";
		rightCol = pal->tx;
	}
	else if (st == ST_SCAN)
	{
		right = "SCAN";
		rightCol = pal->scan;
	}
	else if (keypadLocked && PTTLocked)
	{
		right = "LOCK";
		rightCol = pal->scan;
	}
	else if (keypadLocked)
	{
		right = "KEYL";
		rightCol = pal->scan;
	}
	else if (PTTLocked)
	{
		right = "PTTL";
		rightCol = pal->scan;
	}
	else if (st == ST_TX)
	{
		right = "TX";
		rightCol = pal->tx;
	}

	if (right)
	{
		text((DISPLAY_SIZE_X - 2), (FOOTER_Y + 2), right, FONT_SIZE_1_BOLD, rightCol, true);
	}
}

static void rememberDrawn(emcomState_t st)
{
	drawn.state = (uint8_t)st;
	drawn.volume = (int8_t)volumeLevel();
	drawn.minute = (uiDataGlobal.dateTimeSecs / 60U);
#if defined(I2S_APRS_RX)
	drawn.unread = aprsRxMessageUnreadCount();
	drawn.stations = aprsRxStationCount();
#endif
	drawn.gpsFix = gpsHasFix();
	drawn.sosPhase = (((ticksGetMillis() / 600U) & 1U) == 0U);
	drawn.rxDbm = (int16_t)trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	drawn.lastFull = ticksGetMillis();
	drawn.valid = true;
}

void emcomRenderMain(const emcomMainInfo_t *info)
{
	emcomState_t st = currentState();

	selectPalette();
	if (info->isTransmitting)
	{
		st = ST_TX;
	}

	colour(pal->bg);
	displayFillRect(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, true);

	emcomRenderStatusBar();
	if (st != ST_TX)
	{
		emcomDrawRssi(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	}
	else
	{
		emcomDrawBar(0, true);
	}
	selectPalette();

	drawZoneRow(info);
	drawHero(info, st);
	divider(DIV1_Y);

	if (st == ST_TX)
	{
		drawTxPanel(info);
	}
	else if ((trxGetMode() == RADIO_MODE_DIGITAL) && uiDataGlobal.isDisplayingQSOData)
	{
		drawCallerPanel();
	}
	else if ((st == ST_RX) && (trxGetMode() == RADIO_MODE_ANALOG))
	{
		drawRxPanel();
	}
	else
	{
		drawParams();
	}

	divider(DIV2_Y);
	drawEmcomRows();
	drawFooter(st);

	displayThemeResetToDefault();
	rememberDrawn(st);
	displayRender();
}

bool emcomNeedsFullRedraw(uint32_t now)
{
	emcomState_t st = currentState();

	if (drawn.valid == false)
	{
		return true;
	}

	if (((uint8_t)st != drawn.state) || (volumeLevel() != drawn.volume) ||
			((uiDataGlobal.dateTimeSecs / 60U) != drawn.minute) || (gpsHasFix() != drawn.gpsFix))
	{
		return true;
	}

#if defined(I2S_APRS_RX)
	if ((aprsRxMessageUnreadCount() != drawn.unread) || (aprsRxStationCount() != drawn.stations))
	{
		return true;
	}
#endif

	if (sosOn && ((((now / 600U) & 1U) == 0U) != drawn.sosPhase))
	{
		return true;
	}

	// Live level in the RX panel, at most twice per second
	if ((st == ST_RX) && ((now - drawn.lastFull) >= 500U) && (abs(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY) - drawn.rxDbm) >= 2))
	{
		return true;
	}

	return false;
}

// --- theme preset (menus in the same colours as the EMCOM screen) ------------------------
void emcomThemePreset(bool night, uint16_t *items)
{
	static const uint32_t day[THEME_ITEM_MAX] =
	{
		[THEME_ITEM_FG_DEFAULT] = 0xF2F5F0, [THEME_ITEM_BG] = 0x0B0E0C, [THEME_ITEM_FG_DECORATION] = 0x86928A,
		[THEME_ITEM_FG_TEXT_INPUT] = 0xF2F5F0, [THEME_ITEM_FG_SPLASHSCREEN] = 0xF2F5F0, [THEME_ITEM_BG_SPLASHSCREEN] = 0x0B0E0C,
		[THEME_ITEM_FG_NOTIFICATION] = 0xF2F5F0, [THEME_ITEM_FG_WARNING_NOTIFICATION] = 0xFFB000, [THEME_ITEM_FG_ERROR_NOTIFICATION] = 0xFF3B30,
		[THEME_ITEM_BG_NOTIFICATION] = 0x18201B, [THEME_ITEM_FG_MENU_NAME] = 0xE9EEE7, [THEME_ITEM_BG_MENU_NAME] = 0x18201B,
		[THEME_ITEM_FG_MENU_ITEM] = 0xF2F5F0, [THEME_ITEM_BG_MENU_ITEM_SELECTED] = 0x3BD16F, [THEME_ITEM_FG_OPTIONS_VALUE] = 0x5AC8FA,
		[THEME_ITEM_FG_HEADER_TEXT] = 0xE9EEE7, [THEME_ITEM_BG_HEADER_TEXT] = 0x18201B, [THEME_ITEM_FG_RSSI_BAR] = 0x3BD16F,
		[THEME_ITEM_FG_RSSI_BAR_S9P] = 0xFFB000, [THEME_ITEM_FG_CHANNEL_NAME] = 0xF2F5F0, [THEME_ITEM_FG_CHANNEL_CONTACT] = 0xF2F5F0,
		[THEME_ITEM_FG_CHANNEL_CONTACT_INFO] = 0x5AC8FA, [THEME_ITEM_FG_ZONE_NAME] = 0x86928A, [THEME_ITEM_FG_RX_FREQ] = 0x5AC8FA,
		[THEME_ITEM_FG_TX_FREQ] = 0xFF3B30, [THEME_ITEM_FG_CSS_SQL_VALUES] = 0x86928A, [THEME_ITEM_FG_TX_COUNTER] = 0xFF3B30,
		[THEME_ITEM_FG_POLAR_DRAWING] = 0x86928A, [THEME_ITEM_FG_SATELLITE_COLOUR] = 0x3BD16F, [THEME_ITEM_FG_GPS_NUMBER] = 0xF2F5F0,
		[THEME_ITEM_FG_GPS_COLOUR] = 0x5AC8FA, [THEME_ITEM_FG_BD_COLOUR] = 0xFF3B30
	};

	for (int i = 0; i < THEME_ITEM_MAX; i++)
	{
		uint32_t c = day[i];

		if (night)
		{
			// red only: keep the brightness order of the day palette
			bool isBg = ((i == THEME_ITEM_BG) || (i == THEME_ITEM_BG_SPLASHSCREEN));
			bool isPanel = ((i == THEME_ITEM_BG_NOTIFICATION) || (i == THEME_ITEM_BG_MENU_NAME) || (i == THEME_ITEM_BG_HEADER_TEXT));
			bool isDim = ((i == THEME_ITEM_FG_DECORATION) || (i == THEME_ITEM_FG_ZONE_NAME) || (i == THEME_ITEM_FG_CSS_SQL_VALUES) || (i == THEME_ITEM_FG_POLAR_DRAWING));

			c = (isBg ? 0x000000 : (isPanel ? 0x120202 : (isDim ? 0x7A1414 : ((i == THEME_ITEM_BG_MENU_ITEM_SELECTED) ? 0x8A1818 : 0xD22A2A))));
		}
		items[i] = displayConvertRGB888ToNative(c);
	}
}

// --- SOS ---------------------------------------------------------------------------------
bool emcomSosActive(void)
{
	return sosOn;
}

// Two presses within 3 s are needed (no SOS by accident)
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
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 3000U, (sosOn ? "Again: SOS OFF" : "Again: SOS ON"), false);
	soundSetMelody(MELODY_KEY_BEEP);
}

void emcomSosToggle(void)
{
	sosOn = !sosOn;
	sosSent = 0U;
	sosNextMs = ticksGetMillis();
	drawn.valid = false;

	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2000U, (sosOn ? "SOS: ON" : "SOS: OFF"), false);
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
		uint32_t latMin = (uint32_t)(((alat - latDeg) * 6000.0) + 0.5); // hundredths of minutes
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

		// '\' table, '!' symbol = Emergency
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
			sosNextMs = (ticksGetMillis() + ((sosSent < SOS_FAST_BEACONS) ? SOS_FIRST_INTERVAL_MS : SOS_INTERVAL_MS));
			break;
		case APRS_INFO_TX_NO_CONFIG:
		case APRS_INFO_TX_NOT_ALLOWED:
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2500U,
					((res == APRS_INFO_TX_NO_CONFIG) ? "SOS: no APRS config" : "SOS: TX not allowed"), true);
			sosNextMs = (ticksGetMillis() + SOS_INTERVAL_MS);
			break;
		case APRS_INFO_TX_CHANNEL_BUSY:
			// the busy-channel wait blocks the UI for up to 2 s: do not retry too often
			sosNextMs = (ticksGetMillis() + SOS_BUSY_RETRY_MS);
			break;
		default:
			sosNextMs = (ticksGetMillis() + 5000U); // TX already in progress: retry soon
			break;
	}
}

void emcomSosTick(void)
{
	int menu = menuSystemGetCurrentMenuNumber();

	if ((sosOn == false) || ((int32_t)(ticksGetMillis() - sosNextMs) < 0))
	{
		return;
	}

	if (trxTransmissionEnabled || aprsBeaconingIsTransmitting() ||
			((menu != UI_CHANNEL_MODE) && (menu != UI_VFO_MODE)))
	{
		return;
	}

#if defined(I2S_APRS_RX)
	i2sAprsRxStop(); // restarted by aprsRxTick() after the transmission
#endif
	sosSend();
}

#endif // HAS_EMCOM_UI
