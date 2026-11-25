/*
 * Fork: "EMCOM" main screen style, by Marcin SP7TQ. See uiEmcom.h.
 *
 * Layout (160 x 128, y in pixels):
 *   0..10   status bar: mode / TX / SCAN chip, power, tone or time slot,
 *           APRS, GPS, volume, battery; SOS banner
 *   12..14  S-meter / mic level, 32 segments
 *   17      zone (or VFO) | CH n/m
 *   20..47  channel number (16x32) + name (8x16) + frequency, or VFO frequency
 *   50      divider
 *   54..74  panel: parameters, or RX level, DMR caller, TX timer
 *   75 / 83 compact / expanded lower divider
 *   79,89,99 compact rows; 86,96,106 expanded rows: APRS, position, last heard
 *   116..127 footer: UTC clock, RX / TX / scan / lock / SOS
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
#include "user_interface/uiTextEdit.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/codeplug.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "functions/aprs.h"
#include "functions/eventLog.h"
#include "functions/blackout.h"
#include "functions/batteryRuntime.h"
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
#include "functions/dmrSms.h"
#endif

#if defined(HAS_EMCOM_UI)

extern int8_t lastVolume;

// --- palette ------------------------------------------------------------------------
typedef struct
{
	uint32_t bg, fg, mut, line, bar, barFg, rx, tx, scan, info, seg;
	uint32_t txFg;  // text on the TX colour (TX status bar, SOS banner)
	bool     cues;  // state also by shape: outlined / solid / inverse chips, striped S9+ segments, '!' battery
	bool     large; // bold small text, 8x16 channel frequency, thick S-meter
} emcomPalette_t;

static const emcomPalette_t PALETTE_DARK =
{
	.bg = 0x0B0E0C, .fg = 0xF2F5F0, .mut = 0x86928A, .line = 0x28302B, .bar = 0x18201B, .barFg = 0xE9EEE7,
	.rx = 0x3BD16F, .tx = 0xFF3B30, .scan = 0xFFB000, .info = 0x5AC8FA, .seg = 0x202823,
	.txFg = 0xFFFFFF, .cues = false, .large = false
};
// Night: red only (keeps the night vision, low light signature)
static const emcomPalette_t PALETTE_NIGHT =
{
	.bg = 0x000000, .fg = 0xD22A2A, .mut = 0x7A1414, .line = 0x3A0808, .bar = 0x120202, .barFg = 0xD22A2A,
	.rx = 0xE23A3A, .tx = 0xFF4A4A, .scan = 0xA51C1C, .info = 0xC02626, .seg = 0x250505,
	.txFg = 0xFFFFFF, .cues = false, .large = false
};
// Mono (colour-blind safe): grey shades only, the state is a word plus a shape
static const emcomPalette_t PALETTE_MONO =
{
	.bg = 0x000000, .fg = 0xFFFFFF, .mut = 0x9A9A9A, .line = 0x4A4A4A, .bar = 0x1E1E1E, .barFg = 0xF0F0F0,
	.rx = 0xFFFFFF, .tx = 0xFFFFFF, .scan = 0xB4B4B4, .info = 0xD8D8D8, .seg = 0x343434,
	.txFg = 0x000000, .cues = true, .large = false
};
// Mono at night: the night reds, with the same shape cues (black text on the TX bar)
static const emcomPalette_t PALETTE_MONO_NIGHT =
{
	.bg = 0x000000, .fg = 0xD22A2A, .mut = 0x7A1414, .line = 0x3A0808, .bar = 0x120202, .barFg = 0xD22A2A,
	.rx = 0xE23A3A, .tx = 0xFF4A4A, .scan = 0xA51C1C, .info = 0xC02626, .seg = 0x250505,
	.txFg = 0x000000, .cues = true, .large = false
};
// Contrast (low vision): white and yellow on black, bold text
static const emcomPalette_t PALETTE_CONTRAST =
{
	.bg = 0x000000, .fg = 0xFFFFFF, .mut = 0xFFFF00, .line = 0x808080, .bar = 0x000000, .barFg = 0xFFFFFF,
	.rx = 0xFFFFFF, .tx = 0xFFFF00, .scan = 0xFFFF00, .info = 0xFFFFFF, .seg = 0x404040,
	.txFg = 0x000000, .cues = true, .large = true
};
// Contrast at night: amber and red on black, as bright as the night allows
static const emcomPalette_t PALETTE_CONTRAST_NIGHT =
{
	.bg = 0x000000, .fg = 0xFFA000, .mut = 0xFF4020, .line = 0x802000, .bar = 0x000000, .barFg = 0xFFA000,
	.rx = 0xFFA000, .tx = 0xFFA000, .scan = 0xFF4020, .info = 0xFFA000, .seg = 0x401000,
	.txFg = 0x000000, .cues = true, .large = true
};

#define STATUS_H        11
#define METER_Y         12
#define METER_H         3
#define METER_H_LARGE   4    // contrast style (row 16 stays clear above the zone row)
#define ZONE_Y          17
#define HERO_Y          20   // 16x32 cell, glyphs in rows 6..24
#define NAME_Y          24   // 8x16 cell, glyphs in rows 2..12
#define FREQ_Y          40
#define NAME_Y_LARGE    23   // contrast: name glyphs in rows 25..35,
#define FREQ_Y_LARGE    36   // 8x16 frequency under it in rows 38..48
#define EMCOM_SMALL_ROW_PITCH  10
#define EMCOM_DIV1_Y           50
#define EMCOM_PANEL_Y1         54
#define EMCOM_PANEL_Y2         (EMCOM_PANEL_Y1 + EMCOM_SMALL_ROW_PITCH)
#define EMCOM_PANEL_Y3         (EMCOM_PANEL_Y2 + EMCOM_SMALL_ROW_PITCH)
#define EMCOM_CHIP_TOP_OFFSET (-1)
#define EMCOM_CHIP_HEIGHT      10
#define EMCOM_LARGE_GLYPH_BOTTOM 12

// FONT_SIZE_3 is an 8x16 cell whose normal glyphs occupy rows +2..+12. Start
// it at the divider so its final pixels clear the first small row at 64.
#define EMCOM_PANEL_LARGE_Y1   EMCOM_DIV1_Y

// The inverse chip occupies y-1..y+8. Keeping the third panel row at 74
// therefore leaves row 82 clear before the expanded divider at 83.
#define EMCOM_COMPACT_DIV2_Y  75
#define EMCOM_EXPANDED_DIV2_Y 83

// FONT_SIZE_1 is an 8-pixel cell. The expanded rows end at 113, leaving
// rows 114..115 clear before the 116 footer on 128-line displays.
#define EMCOM_COMPACT_ROW_Y1  79
#define EMCOM_COMPACT_ROW_Y2  (EMCOM_COMPACT_ROW_Y1 + EMCOM_SMALL_ROW_PITCH)
#define EMCOM_COMPACT_ROW_Y3  (EMCOM_COMPACT_ROW_Y2 + EMCOM_SMALL_ROW_PITCH)
#define EMCOM_EXPANDED_ROW_Y1 86
#define EMCOM_EXPANDED_ROW_Y2 (EMCOM_EXPANDED_ROW_Y1 + EMCOM_SMALL_ROW_PITCH)
#define EMCOM_EXPANDED_ROW_Y3 (EMCOM_EXPANDED_ROW_Y2 + EMCOM_SMALL_ROW_PITCH)

#define FOOTER_Y        (DISPLAY_SIZE_Y - 12) // 116 on 128-line screens, 108 on the DM-1701
#define S9_SEGMENT      20
#define COL2_X          66   // parameter columns
#define COL3_X          120

typedef struct
{
	int16_t lowerDividerY;
	int16_t rowY1;
	int16_t rowY2;
	int16_t rowY3;
	bool    expanded;
} emcomLayout_t;

static const emcomLayout_t EMCOM_LAYOUT_COMPACT =
{
	.lowerDividerY = EMCOM_COMPACT_DIV2_Y,
	.rowY1 = EMCOM_COMPACT_ROW_Y1,
	.rowY2 = EMCOM_COMPACT_ROW_Y2,
	.rowY3 = EMCOM_COMPACT_ROW_Y3,
	.expanded = false
};

static const emcomLayout_t EMCOM_LAYOUT_EXPANDED =
{
	.lowerDividerY = EMCOM_EXPANDED_DIV2_Y,
	.rowY1 = EMCOM_EXPANDED_ROW_Y1,
	.rowY2 = EMCOM_EXPANDED_ROW_Y2,
	.rowY3 = EMCOM_EXPANDED_ROW_Y3,
	.expanded = true
};

_Static_assert((EMCOM_PANEL_Y2 - EMCOM_PANEL_Y1) == EMCOM_SMALL_ROW_PITCH, "EMCOM panel pitch");
_Static_assert((EMCOM_PANEL_Y3 - EMCOM_PANEL_Y2) == EMCOM_SMALL_ROW_PITCH, "EMCOM panel pitch");
_Static_assert((EMCOM_PANEL_LARGE_Y1 + EMCOM_LARGE_GLYPH_BOTTOM + 1) <= EMCOM_PANEL_Y2,
		"EMCOM large panel glyph clearance");
_Static_assert(EMCOM_EXPANDED_DIV2_Y >= (EMCOM_PANEL_Y3 + EMCOM_CHIP_TOP_OFFSET + EMCOM_CHIP_HEIGHT),
		"EMCOM expanded panel clearance");
_Static_assert((EMCOM_COMPACT_ROW_Y3 + FONT_SIZE_1_HEIGHT + 1) <= FOOTER_Y, "EMCOM compact rows fit");
_Static_assert((DISPLAY_SIZE_Y < 128) || ((EMCOM_EXPANDED_ROW_Y3 + FONT_SIZE_1_HEIGHT + 2) <= FOOTER_Y), "EMCOM expanded rows fit");

typedef enum
{
	ST_STBY = 0,
	ST_RX,
	ST_TX,
	ST_SCAN
} emcomState_t;

typedef enum
{
	EMCOM_PANEL_PARAMS = 0,
	EMCOM_PANEL_DETAILS,
	EMCOM_PANEL_DMR_RX,
	EMCOM_PANEL_FM_RX,
	EMCOM_PANEL_TX
} emcomPanel_t;

typedef enum
{
	EMCOM_POSITION_NONE = 0,
	EMCOM_POSITION_GPS,
	EMCOM_POSITION_CONFIGURED
} emcomPositionSource_t;

typedef struct
{
	bool                    valid;
	emcomPositionSource_t   source;
	double                  latitude;
	double                  longitude;
} emcomPosition_t;

#define EMCOM_POSITION_TEXT_SIZE   24
#define EMCOM_MAIDENHEAD_SIZE      7
#define EMCOM_DISTANCE_TEXT_SIZE   16
#define EMCOM_POSITION_REFRESH_MS  1000U

typedef struct
{
	bool                    valid;
	emcomPositionSource_t   source;
	char                    positionText[EMCOM_POSITION_TEXT_SIZE];
	char                    maidenhead[EMCOM_MAIDENHEAD_SIZE];
	bool                    repeaterDistanceVisible;
	char                    repeaterDistance[EMCOM_DISTANCE_TEXT_SIZE];
	bool                    aprsDistanceVisible;
	char                    aprsDistance[EMCOM_DISTANCE_TEXT_SIZE];
} emcomPositionDisplay_t;

static const emcomPalette_t *pal = &PALETTE_DARK;
static bool vfoAllowed = true;
static bool channelAllowed = true;
static const emcomLayout_t *lastRenderedLayout = &EMCOM_LAYOUT_COMPACT;
static const emcomLayout_t *lastTxLayout =
		((DISPLAY_SIZE_Y >= 128) ? &EMCOM_LAYOUT_EXPANDED : &EMCOM_LAYOUT_COMPACT);
static emcomPanel_t lastRenderedPanel = EMCOM_PANEL_PARAMS;

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
	uint8_t  radioMode;
	emcomPanel_t panel;
	bool     isVFO;
	int16_t  zoneIndex;
	int16_t  channelIndex;
	uint32_t rxFreq;
	uint32_t txFreq;
	bool     positionVisible;
	bool     positionValid;
	emcomPositionSource_t positionSource;
	char     positionText[EMCOM_POSITION_TEXT_SIZE];
	char     maidenhead[EMCOM_MAIDENHEAD_SIZE];
	bool     repeaterDistanceVisible;
	char     repeaterDistance[EMCOM_DISTANCE_TEXT_SIZE];
	bool     aprsDistanceVisible;
	char     aprsDistance[EMCOM_DISTANCE_TEXT_SIZE];
	bool     distanceEnabled;
	uint32_t lastFull;
	uint32_t lastPositionRendered;
	bool     valid;
	bool     blackout;
} drawn;

// SOS
#define SOS_FIRST_INTERVAL_MS   60000U
#define SOS_INTERVAL_MS         120000U
#define SOS_FAST_BEACONS        3U
#define SOS_BUSY_RETRY_MS       20000U
static bool sosOn = false;
static uint32_t sosNextMs;
static uint32_t sosSent;
static bool sosPauseShown; // "SOS PAUSED" shown for the beacon that is due

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

// Draws 'str' (clipped to the screen) in exactly 'font', returns its width. alignRight: x is the right edge.
static int16_t textRaw(int16_t x, int16_t y, const char *str, ucFont_t font, uint32_t rgb, bool alignRight)
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

// As textRaw(), the contrast style uses the bold small font (same width)
static int16_t text(int16_t x, int16_t y, const char *str, ucFont_t font, uint32_t rgb, bool alignRight)
{
	return textRaw(x, y, str, (((font == FONT_SIZE_1) && pal->large) ? FONT_SIZE_1_BOLD : font), rgb, alignRight);
}

// Inverse video chip: 'str' in 'textRgb' (the colour behind the chip) on a 'rgb' block (y is the text row)
static int16_t inverseChip(int16_t x, int16_t y, const char *str, uint32_t rgb, uint32_t textRgb, bool alignRight)
{
	int16_t w = (int16_t)((strlen(str) * 6) + 4);

	if (alignRight)
	{
		x -= w;
	}
	fill(x, (y + EMCOM_CHIP_TOP_OFFSET), w, EMCOM_CHIP_HEIGHT, rgb);
	textRaw((x + 2), y, str, FONT_SIZE_1_BOLD, textRgb, false);
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
	bool night = (DAYTIME_CURRENT == NIGHT);

	switch (emcomStyleGet())
	{
		case EMCOM_STYLE_MONO:
			pal = (night ? &PALETTE_MONO_NIGHT : &PALETTE_MONO);
			break;
		case EMCOM_STYLE_CONTRAST:
			pal = (night ? &PALETTE_CONTRAST_NIGHT : &PALETTE_CONTRAST);
			break;
		default:
			pal = (night ? &PALETTE_NIGHT : &PALETTE_DARK);
			break;
	}
}

// --- state ----------------------------------------------------------------------------
// Classic bit set: classic. Else the variant bits (both clear in settings from before the
// variants existed, so those keep the plain EMCOM style).
emcomStyle_t emcomStyleGet(void)
{
	if (settingsIsOptionBitSet(BIT_UI_CLASSIC_STYLE))
	{
		return EMCOM_STYLE_CLASSIC;
	}
	if (settingsIsOptionBitSet(BIT_UI_EMCOM_CONTRAST))
	{
		return EMCOM_STYLE_CONTRAST;
	}
	if (settingsIsOptionBitSet(BIT_UI_EMCOM_MONO))
	{
		return EMCOM_STYLE_MONO;
	}
	return EMCOM_STYLE_EMCOM;
}

void emcomStyleSet(emcomStyle_t style)
{
	settingsSetOptionBit(BIT_UI_CLASSIC_STYLE, (style == EMCOM_STYLE_CLASSIC));
	settingsSetOptionBit(BIT_UI_EMCOM_MONO, (style == EMCOM_STYLE_MONO));
	settingsSetOptionBit(BIT_UI_EMCOM_CONTRAST, (style == EMCOM_STYLE_CONTRAST));
}

const char *emcomStyleName(emcomStyle_t style)
{
	static const char *const names[EMCOM_STYLE_COUNT] = { "Classic", "EMCOM", "EMCOM mono", "EMCOM contr" };

	return names[((style < EMCOM_STYLE_COUNT) ? style : EMCOM_STYLE_EMCOM)];
}

bool emcomStyleEnabled(void)
{
	return (settingsIsOptionBitSet(BIT_UI_CLASSIC_STYLE) == false);
}

bool emcomShapeCues(void)
{
	emcomStyle_t style = emcomStyleGet();

	return ((style == EMCOM_STYLE_MONO) || (style == EMCOM_STYLE_CONTRAST));
}

// Applies the screen style setting to the whole UI: flat boxes and the default colours
// (a theme saved by the user in the custom data still wins).
void emcomApplyStyle(void)
{
	displaySetFlatStyle(emcomStyleEnabled());
	themeInit(true);
	displayThemeResetToDefault();
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

static bool ownPositionInfo(emcomPosition_t *position)
{
	if (position == NULL)
	{
		return false;
	}

	memset(position, 0, sizeof(*position));
	position->source = EMCOM_POSITION_NONE;

#if defined(I2S_APRS_RX)
	bool gpsPositionAvailable = false;

#if defined(HAS_GPS)
	gpsPositionAvailable = ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION)) ==
			(GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION));
#endif

	if (aprsRxOwnPosition(&position->latitude, &position->longitude) &&
			emcomCoordinateIsValid(position->latitude, position->longitude))
	{
		position->valid = true;
		position->source = (gpsPositionAvailable ? EMCOM_POSITION_GPS : EMCOM_POSITION_CONFIGURED);
		return true;
	}

	// aprsRxOwnPosition() is GPS-first, but validate its result before using
	// it and retain the configured-location fallback for an invalid GPS fix.
#endif
	if (settingsLocationIsValid())
	{
		position->latitude = settingsLocationGetLatitude();
		position->longitude = settingsLocationGetLongitude();
		if (emcomCoordinateIsValid(position->latitude, position->longitude))
		{
			position->valid = true;
			position->source = EMCOM_POSITION_CONFIGURED;
			return true;
		}
	}
	return false;
}

static bool ownPosition(double *lat, double *lon)
{
	emcomPosition_t position;

	if (ownPositionInfo(&position) == false)
	{
		return false;
	}

	*lat = position.latitude;
	*lon = position.longitude;
	return true;
}

static bool dmrCallerPanelActive(void)
{
	return ((trxGetMode() == RADIO_MODE_DIGITAL) && uiDataGlobal.isDisplayingQSOData &&
			(LinkHead != NULL) && (HRC6000GetReceivedTgOrPcId() != 0));
}

// Distance is a channel-mode feature. Keep this decision tied to the panel
// selected for this render so VFO reception and higher-priority panels cannot
// accidentally inherit the distance layout. The row remains reserved on the
// channel standby/FM RX/scan parameter layouts even when the setting is off,
// so rows below it do not move; drawDistanceLine() leaves that row empty.
static bool channelDistanceRowVisible(const emcomMainInfo_t *info, emcomState_t st, emcomPanel_t panel)
{
	if ((info == NULL) || info->isVFO || (menuSystemGetRootMenuNumber() != UI_CHANNEL_MODE) ||
			(DISPLAY_SIZE_Y < 128))
	{
		return false;
	}

	if (panel == EMCOM_PANEL_FM_RX)
	{
		return ((st == ST_RX) && (trxGetMode() == RADIO_MODE_ANALOG));
	}

	return ((panel == EMCOM_PANEL_PARAMS) && (st != ST_TX) && (info->showDetails == false) &&
			(info->gotoChannel == 0) && (info->freqEntry == NULL));
}

static bool channelDistanceDisplayEligible(const emcomMainInfo_t *info, emcomState_t st, emcomPanel_t panel)
{
	return (channelDistanceRowVisible(info, st, panel) &&
			settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE));
}

static emcomPanel_t renderedPanel(const emcomMainInfo_t *info, emcomState_t st)
{
	if (st == ST_TX)
	{
		return EMCOM_PANEL_TX;
	}

	if (info->showDetails)
	{
		return EMCOM_PANEL_DETAILS;
	}

	// drawCallerPanel() falls back to drawParams() when the QSO panel has no
	// caller data, so only classify it as DMR RX when it will show that panel.
	if (dmrCallerPanelActive())
	{
		return EMCOM_PANEL_DMR_RX;
	}

	if ((st == ST_RX) && (trxGetMode() == RADIO_MODE_ANALOG))
	{
		return EMCOM_PANEL_FM_RX;
	}

	return EMCOM_PANEL_PARAMS;
}

// The redraw poll does not have the full emcomMainInfo_t passed to the render
// entry point, but it still needs to notice a DMR QSO panel ending while the
// radio state is in the short digital-RX tail. Keep this equivalent to the
// panel precedence used by renderedPanel().
static emcomPanel_t currentPanelForRedraw(emcomState_t st)
{
	if (st == ST_TX)
	{
		return EMCOM_PANEL_TX;
	}

	if (uiDataGlobal.displayChannelSettings)
	{
		return EMCOM_PANEL_DETAILS;
	}

	if (dmrCallerPanelActive())
	{
		return EMCOM_PANEL_DMR_RX;
	}

	if ((st == ST_RX) && (trxGetMode() == RADIO_MODE_ANALOG))
	{
		return EMCOM_PANEL_FM_RX;
	}

	return EMCOM_PANEL_PARAMS;
}

static const emcomLayout_t *layoutForPanel(emcomPanel_t panel, bool distanceRowVisible)
{
	bool expanded = distanceRowVisible;

	if (DISPLAY_SIZE_Y >= 128)
	{
		switch (panel)
		{
			case EMCOM_PANEL_DMR_RX:
			case EMCOM_PANEL_TX:
				expanded = true;
				break;
			default:
				break;
		}
	}

	return (expanded ? &EMCOM_LAYOUT_EXPANDED : &EMCOM_LAYOUT_COMPACT);
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

static bool volumeIndicatorVisible(void)
{
	// The header always shows the volume. BIT_VISUAL_VOLUME is the upstream "volume pop-up on change"
	// option (applicationMain.c updateVolumeGain), off by default, so it must not hide this indicator.
	return true;
}

static int visibleVolumeLevel(void)
{
	return (volumeIndicatorVisible() ? volumeLevel() : -1);
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

void emcomRenderStatusBar(bool forceBatteryDisplay)
{
	char buf[16];
	emcomState_t st = currentState();
	bool isTx = (st == ST_TX);
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	bool batteryIsLow = batteryIsLowWarning();
	bool displayTime = (settingsIsOptionBitSet(BIT_DISPLAY_TIME_IN_HEADER) && (forceBatteryDisplay == false) && (batteryIsLow == false));
	uint32_t barCol, fg;
	int16_t x;

	selectPalette();
	bool colourStateHeader = ((DAYTIME_CURRENT != NIGHT) && (pal->cues == false));
	bool colourStateRxHeader = (colourStateHeader && (st == ST_RX));
	barCol = (isTx ? pal->tx : (colourStateRxHeader ? pal->rx : pal->bar));
	fg = (isTx ? pal->txFg : (colourStateRxHeader ? pal->bar : pal->barFg));

	fill(0, 0, DISPLAY_SIZE_X, STATUS_H, barCol);
	fill(0, STATUS_H, DISPLAY_SIZE_X, 1, pal->bg); // the classic header clear uses the theme colour

	if (sosOn && (st != ST_TX))
	{
		bool phase = (((ticksGetMillis() / 600U) & 1U) == 0U);

		fill(0, 0, DISPLAY_SIZE_X, STATUS_H, (phase ? pal->tx : pal->bar));
		text(((DISPLAY_SIZE_X - (13 * 6)) / 2), 2, "EMERGENCY SOS", FONT_SIZE_1_BOLD, (phase ? pal->txFg : pal->tx), false);
		displayThemeResetToDefault();
		return;
	}

	// State chip: the mode when idle or receiving, else the state. RX is
	// repeated in the footer with the TX and SCAN indicators.
	const char *mode = (digital ? "DMR" : (trxGetBandwidthIs25kHz() ? "FM" : "FMN"));
	const char *chip = ((st == ST_TX) ? "TX" : ((st == ST_SCAN) ? "SCAN" : mode));
	uint32_t chipCol;
	uint32_t chipTextCol;
	bool colourStateChip = colourStateHeader;

	if (colourStateChip && (st == ST_TX))
	{
		chipCol = pal->tx;
		chipTextCol = pal->txFg;
	}
	else if (colourStateChip && (st == ST_RX))
	{
		chipCol = pal->rx;
		chipTextCol = pal->bar;
	}
	else
	{
		chipCol = ((st == ST_TX) ? pal->txFg : ((st == ST_SCAN) ? pal->scan : pal->barFg));
		chipTextCol = (isTx ? pal->tx : pal->bar);
	}
	int16_t chipW = (int16_t)((strlen(chip) * 6) + 4);

	if (pal->cues && (st == ST_STBY))
	{
		// mono / contrast: idle = outlined mode chip, SCAN = solid chip, TX = inverse status bar
		colour(chipCol);
		displayDrawRect(1, 1, chipW, 9, false);
		text(3, 2, chip, FONT_SIZE_1_BOLD, chipCol, false);
	}
	else
	{
		fill(1, 1, chipW, 9, chipCol);
		text(3, 2, chip, FONT_SIZE_1_BOLD, chipTextCol, false);
	}
	x = chipW + 4;

	if ((st != ST_STBY) && (st != ST_RX))
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
		textRaw(x, 2, "a", (beaconing ? FONT_SIZE_1_BOLD : FONT_SIZE_1), (isTx ? fg : (beaconing ? (colourStateRxHeader ? fg : pal->rx) : pal->mut)), false);
	}

	// Right side: time or battery, volume, GPS. The Display menu controls whether
	// time replaces the battery readout and whether that readout is percent or voltage.
	int16_t r = (DISPLAY_SIZE_X - 2);
	bool low = batteryIsLow;
	uint32_t battCol = (low ? pal->tx : fg);

	if (displayTime)
	{
		struct tm t;
		time_t_custom secs = uiDataGlobal.dateTimeSecs;

		gmtime_r_Custom(&secs, &t);
		snprintf(buf, sizeof(buf), "%02d%c%02d", t.tm_hour, (((t.tm_sec % 2) == 0) ? ':' : ' '), t.tm_min);
		r -= text(r, 2, buf, FONT_SIZE_1, fg, true) + 4;
	}
	else
	{
		int pct = getBatteryPercentage();

		if (pal->cues)
		{
			battCol = fg; // low battery: '!' and an inverse chip instead of a colour
		}
		colour(battCol);
		displayDrawRect((r - 13), 2, 12, 7, false);
		displayFillRect((r - 1), 4, 1, 3, false);
		displayFillRect((r - 12), 3, (int16_t)CLAMP(((pct * 10) + 50) / 100, 0, 10), 5, false);
		r -= 16;
		if (pal->cues && low)
		{
			snprintf(buf, sizeof(buf), "!%d%%", pct);
			r -= inverseChip((r + 2), 2, buf, fg, barCol, true) + 2;
		}
		else if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
		{
			int volts;
			int mvolts;

			getBatteryVoltage(&volts, &mvolts);
			snprintf(buf, sizeof(buf), "%2d.%1dV", volts, mvolts);
			r -= text(r, 2, buf, FONT_SIZE_1, battCol, true) + 4;
		}
		else
		{
			snprintf(buf, sizeof(buf), "%d%%", pct);
			r -= text(r, 2, buf, FONT_SIZE_1, battCol, true) + 4;
		}
	}

	if (volumeIndicatorVisible())
	{
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
	}

#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_ON)
	{
		bool fix = gpsHasFix();
		// mono / contrast: no fix also in the thin font
		textRaw(r, 2, "GPS", ((fix || (pal->cues == false)) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), (fix ? (isTx ? fg : (colourStateRxHeader ? fg : pal->rx)) : pal->mut), true);
	}
#endif

	displayThemeResetToDefault();
}

// --- S-meter / mic level -------------------------------------------------------------------------
void emcomDrawBar(int barWidth, bool isTx)
{
	int lit = CLAMP((barWidth + 2) / 5, 0, 32);
	int16_t h;

	selectPalette();
	h = (pal->large ? METER_H_LARGE : METER_H);
	for (int i = 0; i < 32; i++)
	{
		uint32_t c = pal->seg;

		if (i < lit)
		{
			c = (isTx ? pal->tx : ((i < S9_SEGMENT) ? pal->rx : pal->scan));
		}

		if (pal->cues && (i < lit) && (isTx == false) && (i >= S9_SEGMENT))
		{
			// mono / contrast: over S9 the segments are hollow
			fill((int16_t)(i * 5), METER_Y, 4, h, pal->seg);
			colour(c);
			displayDrawRect((int16_t)(i * 5), METER_Y, 4, h, false);
		}
		else
		{
			fill((int16_t)(i * 5), METER_Y, 4, h, c);
		}
	}
	fill(0, (METER_Y + h), DISPLAY_SIZE_X, 1, pal->bg);
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

	if (info->isVFO && info->freqEntry)
	{
		// frequency being typed: 3 + 5 digits, cursor under the next one
		const char *d = info->freqEntry;
		int idx = info->freqEntryIndex;

		snprintf(buf, sizeof(buf), "%c%c%c.%c%c%c%c%c", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
		text(1, HERO_Y, buf, FONT_SIZE_4, pal->scan, false);
		if (idx < 8)
		{
			fill((int16_t)(1 + ((idx + ((idx > 2) ? 1 : 0)) * 16) + 2), (HERO_Y + 26), 12, 2, pal->scan);
		}
		return;
	}

	if ((info->isVFO == false) && (info->gotoChannel > 0))
	{
		snprintf(buf, sizeof(buf), "%d", info->gotoChannel);
		int16_t w = text(1, HERO_Y, buf, FONT_SIZE_4, pal->scan, false);
		text((w + 6), NAME_Y, "GO TO CH", FONT_SIZE_3, pal->scan, false);
		text((w + 6), FREQ_Y, "GRN OK  RED CANCEL", FONT_SIZE_1, pal->mut, false);
		return;
	}

	if (info->isVFO && info->entryTx)
	{
		txFreqShown = true; // TX frequency selected (UP / DOWN / knob change it)
	}

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
	text(nameX, (pal->large ? NAME_Y_LARGE : NAME_Y), name, FONT_SIZE_3, pal->fg, false);

	const char *suffix = (txFreqShown ? " TX" : (uiDataGlobal.reverseRepeaterChannel ? " REV" : (uiDataGlobal.talkaround ? " TA" : "")));
	uint32_t freqCol = (txFreqShown ? pal->tx : pal->info);

	if (pal->large)
	{
		// contrast: the frequency in 8x16 under the name, without the unit if it does not fit
		for (int withUnit = 1; withUnit >= 0; withUnit--)
		{
			formatFreq((txFreqShown ? txFreq : rxFreq), buf, sizeof(buf), (withUnit != 0));
			strncat(buf, suffix, (sizeof(buf) - strlen(buf) - 1));
			if (strlen(buf) <= maxChars)
			{
				text(nameX, FREQ_Y_LARGE, buf, FONT_SIZE_3, freqCol, false);
				return;
			}
		}
	}

	formatFreq((txFreqShown ? txFreq : rxFreq), buf, sizeof(buf), true);
	strncat(buf, suffix, (sizeof(buf) - strlen(buf) - 1));
	text(nameX, FREQ_Y, buf, FONT_SIZE_1, freqCol, false);
}

static void drawZoneRow(const emcomMainInfo_t *info)
{
	char buf[24];

	if (info->isVFO)
	{
		snprintf(buf, sizeof(buf), "VFO %c%s", info->vfoLetter, (info->freqEntry ? (info->entryTx ? "  ENTER TX" : "  ENTER RX") : (info->entryTx ? "  TX" : "")));
		text(2, ZONE_Y, buf, FONT_SIZE_1, ((info->freqEntry || info->entryTx) ? pal->scan : pal->mut), false);

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
		keyValue(2, EMCOM_PANEL_Y1, (((PCorTG >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), buf, pal->fg);
		snprintf(buf, sizeof(buf), "%u", (unsigned)trxGetDMRColourCode());
		keyValue(COL2_X, EMCOM_PANEL_Y1, "CC", buf, pal->fg);
		snprintf(buf, sizeof(buf), "%d", (trxGetDMRTimeSlot() + 1));
		keyValue(COL3_X, EMCOM_PANEL_Y1, "TS", buf, pal->fg);

		codeplugUtilConvertBufToString(currentContactData.name, contact, 16);
		if (nonVolatileSettings.overrideTG != 0)
		{
			uiUtilityBuildTgOrPCDisplayName(contact, sizeof(contact));
		}
		contact[16] = 0;
		text(2, EMCOM_PANEL_Y2, contact, FONT_SIZE_1, pal->fg, false);
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
	keyValue(2, EMCOM_PANEL_Y1, "OFS", buf, pal->fg);

	toneString(currentChannelData->rxTone, buf, sizeof(buf));
	keyValue(COL2_X, EMCOM_PANEL_Y1, "RXT", buf, pal->fg);

	uint8_t sql = ((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql);
	snprintf(buf, sizeof(buf), "%d", (int)((sql > 0) ? (5 * (sql - 1)) : 0));
	keyValue(COL3_X, EMCOM_PANEL_Y1, "SQL", buf, pal->fg);

	keyValue(2, EMCOM_PANEL_Y2, "BW", (trxGetBandwidthIs25kHz() ? "25k" : "12.5k"), pal->fg);

	toneString(currentChannelData->txTone, buf, sizeof(buf));
	keyValue(COL2_X, EMCOM_PANEL_Y2, "TXT", buf, pal->fg);

	if (currentChannelData->tot == 0)
	{
		snprintf(buf, sizeof(buf), "off");
	}
	else
	{
		totString(buf, sizeof(buf));
	}
	keyValue(COL3_X, EMCOM_PANEL_Y2, "TOT", buf, pal->fg);
}

// SK1 held: both frequencies with their tones / CC and TS
static void drawDetailsPanel(void)
{
	char f[16], t[16];
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);

	fill(0, (EMCOM_PANEL_Y1 - 1), 2, ((EMCOM_PANEL_Y2 + 9) - (EMCOM_PANEL_Y1 - 1)), pal->info);

	formatFreq(currentChannelData->rxFreq, f, sizeof(f), false);
	if (digital)
	{
		snprintf(t, sizeof(t), "CC%u", (unsigned)trxGetDMRColourCode());
	}
	else
	{
		toneString(currentChannelData->rxTone, t, sizeof(t));
	}
	keyValue(6, EMCOM_PANEL_Y1, "RX", f, pal->fg);
	text((DISPLAY_SIZE_X - 2), EMCOM_PANEL_Y1, t, FONT_SIZE_1, pal->info, true);

	formatFreq(currentChannelData->txFreq, f, sizeof(f), false);
	if (digital)
	{
		snprintf(t, sizeof(t), "TS%d", (trxGetDMRTimeSlot() + 1));
	}
	else
	{
		toneString(currentChannelData->txTone, t, sizeof(t));
	}
	keyValue(6, EMCOM_PANEL_Y2, "TX", f, pal->fg);
	text((DISPLAY_SIZE_X - 2), EMCOM_PANEL_Y2, t, FONT_SIZE_1, pal->info, true);
}

static bool channelLocation(double *lat, double *lon)
{
	uint32_t fixedLat;
	uint32_t fixedLon;

	// Keep the codeplug's existing convention: either zero component means
	// that the channel has no configured location.
	if (codeplugChannelLocationIsValid(currentChannelData) == false)
	{
		return false;
	}

	fixedLat = ((currentChannelData->locationLat2 << 16) | (currentChannelData->locationLat1 << 8) | currentChannelData->locationLat0);
	fixedLon = ((currentChannelData->locationLon2 << 16) | (currentChannelData->locationLon1 << 8) | currentChannelData->locationLon0);
	*lat = latLonFixed24ToDouble(fixedLat);
	*lon = latLonFixed24ToDouble(fixedLon);
	return emcomCoordinateIsValid(*lat, *lon);
}

static void formatPosition(double lat, double lon, char *buf, size_t size)
{
	uint32_t la = (uint32_t)((fabs(lat) * 10000.0) + 0.5);
	uint32_t lo = (uint32_t)((fabs(lon) * 10000.0) + 0.5);

	snprintf(buf, size, "%u.%04u%c %u.%04u%c", (unsigned)(la / 10000U), (unsigned)(la % 10000U), ((lat >= 0.0) ? 'N' : 'S'),
			(unsigned)(lo / 10000U), (unsigned)(lo % 10000U), ((lon >= 0.0) ? 'E' : 'W'));
}

static bool aprsLastHeardCanShowDistance(void)
{
#if defined(I2S_APRS_RX)
	return (((trxGetMode() != RADIO_MODE_DIGITAL) || (uiDataGlobal.lastHeardCount == 0) || (LinkHead == NULL)) &&
			(aprsRxStationCount() > 0));
#else
	return false;
#endif
}

static void buildPositionDisplay(const emcomPosition_t *position, bool repeaterDistanceVisible,
		emcomPositionDisplay_t *display)
{
	memset(display, 0, sizeof(*display));
	display->valid = position->valid;
	display->source = position->source;
	snprintf(display->repeaterDistance, sizeof(display->repeaterDistance), "--");
	snprintf(display->aprsDistance, sizeof(display->aprsDistance), "--");

	if (position->valid)
	{
		coordsToMaidenhead((uint8_t *)display->maidenhead, position->latitude, position->longitude);
		display->maidenhead[EMCOM_MAIDENHEAD_SIZE - 1] = 0;
		formatPosition(position->latitude, position->longitude, display->positionText, sizeof(display->positionText));
	}

	if (repeaterDistanceVisible)
	{
		double targetLat, targetLon;

		display->repeaterDistanceVisible = true;
		if (position->valid && channelLocation(&targetLat, &targetLon))
		{
			emcomFormatDistanceMeters(distanceBetweenTwoCoords(position->latitude, position->longitude,
					targetLat, targetLon, false), display->repeaterDistance, sizeof(display->repeaterDistance));
		}
	}

#if defined(I2S_APRS_RX)
	if (position->valid && aprsLastHeardCanShowDistance())
	{
		const aprsRxStation_t *station = aprsRxStationGet(0);

		if ((station != NULL) && station->hasPos)
		{
			aprsRxFormatDistance(aprsRxDistanceKm(position->latitude, position->longitude, station->lat, station->lon),
					display->aprsDistance, sizeof(display->aprsDistance));
			display->aprsDistanceVisible = true;
		}
	}
#endif
}

static bool positionDisplayChanged(const emcomPositionDisplay_t *position)
{
	if (position->valid != drawn.positionValid)
	{
		return true;
	}

	if (position->valid && ((strcmp(position->positionText, drawn.positionText) != 0) ||
			(strcmp(position->maidenhead, drawn.maidenhead) != 0)))
	{
		return true;
	}

	if ((position->repeaterDistanceVisible != drawn.repeaterDistanceVisible) ||
			(position->repeaterDistanceVisible && (strcmp(position->repeaterDistance, drawn.repeaterDistance) != 0)))
	{
		return true;
	}

	return ((position->aprsDistanceVisible != drawn.aprsDistanceVisible) ||
			(position->aprsDistanceVisible && (strcmp(position->aprsDistance, drawn.aprsDistance) != 0)));
}

static void drawDistanceLine(const emcomPositionDisplay_t *position)
{
	if ((position == NULL) || (position->repeaterDistanceVisible == false))
	{
		return;
	}

	keyValue(2, EMCOM_PANEL_Y3, "DIST", position->repeaterDistance,
			(strcmp(position->repeaterDistance, "--") == 0) ? pal->mut : pal->fg);
}

static void drawRxPanel(bool showDistanceRow, const emcomPositionDisplay_t *position)
{
	char buf[16];
	int dBm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

	snprintf(buf, sizeof(buf), "%ddBm", dBm);
	text(2, EMCOM_PANEL_LARGE_Y1, buf, FONT_SIZE_3, pal->rx, false);
	sUnits(dBm, buf, sizeof(buf));
	text((DISPLAY_SIZE_X - 2), EMCOM_PANEL_LARGE_Y1, buf, FONT_SIZE_3, pal->rx, true);

	if (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE)
	{
		toneString(currentChannelData->rxTone, buf, sizeof(buf));
		keyValue(2, EMCOM_PANEL_Y2, "RXT", buf, pal->rx);
	}
	else
	{
		text(2, EMCOM_PANEL_Y2, "CARRIER", FONT_SIZE_1, pal->mut, false);
	}

	if (showDistanceRow)
	{
		drawDistanceLine(position);
	}
}

static void drawCallerPanel(const emcomLayout_t *layout)
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
	text(2, EMCOM_PANEL_LARGE_Y1, call, FONT_SIZE_3, pal->rx, false);

	if ((LinkHead->talkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		text((DISPLAY_SIZE_X - 2), EMCOM_PANEL_Y1, "PRIVATE", FONT_SIZE_1_BOLD, pal->scan, true);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%u", (unsigned)(LinkHead->talkGroupOrPcId & 0xFFFFFF));
		int16_t w = (int16_t)((strlen(buf) + 3) * 6);
		keyValue((DISPLAY_SIZE_X - 2 - w), EMCOM_PANEL_Y1, "TG", buf, pal->fg);
	}

	// Rest of the contact / TA text (name, place) uses the two small lines
	// below the callsign. Keep words together where possible.
	const char *rest = src;
	while (*rest && (*rest != ' '))
	{
		rest++;
	}
	while (*rest == ' ')
	{
		rest++;
	}

	char callerLine1[27];
	char callerLine2[27];
	size_t restLen = strlen(rest);
	size_t line1Len = MIN(restLen, (sizeof(callerLine1) - 1));

	if (restLen > line1Len)
	{
		size_t split = line1Len;
		while ((split > 0) && (rest[split] != ' '))
		{
			split--;
		}
		if (split >= 8)
		{
			line1Len = split;
		}
	}

	memcpy(callerLine1, rest, line1Len);
	callerLine1[line1Len] = 0;
	while ((line1Len > 0) && (callerLine1[line1Len - 1] == ' '))
	{
		callerLine1[--line1Len] = 0;
	}

	const char *line2 = rest + line1Len;
	while (*line2 == ' ')
	{
		line2++;
	}
	snprintf(callerLine2, sizeof(callerLine2), "%s", line2);
	callerLine2[sizeof(callerLine2) - 1] = 0;
	text(2, EMCOM_PANEL_Y2, callerLine1, FONT_SIZE_1, pal->fg, false);
	if (layout->expanded && (callerLine2[0] != 0))
	{
		text(2, EMCOM_PANEL_Y3, callerLine2, FONT_SIZE_1, pal->fg, false);
	}
}

// Warnings (TOT last seconds, warning cards): amber, at night the brighter TX red (the night amber is too dim for
// small text on it)
static uint32_t warnColour(void)
{
	return (((DAYTIME_CURRENT == NIGHT) && (pal->large == false)) ? pal->tx : pal->scan);
}

// Diagonal stripes in a box (the TOT bar in its last seconds: shown by shape, not only by colour)
static void hatch(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rgb)
{
	colour(rgb);
	for (int16_t j = 0; j < h; j++)
	{
		for (int16_t i = (int16_t)(((y + j) + x) % 3); i < w; i += 3)
		{
			displaySetPixel((x + i), (y + j), false);
		}
	}
}

// "1:05" / "12:34" / "1:02:03"
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

#define TX_TIMER_Y      46   // 16x32 cell: digits in rows 52..70
#define TX_WARN_SECS    10   // TOT: last seconds shown hatched, the countdown in an inverse chip

// TX: elapsed time (large) and the power level, between the dividers
static void drawTxPanel(const emcomMainInfo_t *info, int elapsed)
{
	char buf[16];
	uint8_t powerLevel = trxGetPowerLevel();

	UNUSED_PARAMETER(info);
	mmss(elapsed, buf, sizeof(buf));
	text(1, TX_TIMER_Y, buf, ((elapsed >= 3600) ? FONT_SIZE_3 : FONT_SIZE_4), pal->tx, false);

	text((DISPLAY_SIZE_X - 2), (EMCOM_PANEL_Y1 - 1), "PWR", FONT_SIZE_1, pal->mut, true);
	snprintf(buf, sizeof(buf), "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
	text((DISPLAY_SIZE_X - 2), (EMCOM_PANEL_Y1 + 6), buf, FONT_SIZE_3, pal->fg, true);
}

// TX: the time-out timer bar (remaining time, hatched in the last seconds), the TG / tone and the key hints,
// in place of the EMCOM rows
static void drawTxRows(const emcomLayout_t *layout, int remaining)
{
	char buf[24];
	bool digital = (trxGetMode() == RADIO_MODE_DIGITAL);
	int16_t barX = 26;
	int16_t barW = 86;
	int16_t barH = (pal->large ? 9 : 7);
	int16_t barY = (layout->rowY1 - (pal->large ? 1 : 0));

	text(2, layout->rowY1, "TOT", FONT_SIZE_1, pal->mut, false);
	if (remaining < 0)
	{
		text(barX, layout->rowY1, "off", FONT_SIZE_1, pal->mut, false);
	}
	else
	{
		int total = (int)(currentChannelData->tot * 15U);
		bool last = (remaining <= TX_WARN_SECS);
		// last seconds: the bar becomes a countdown of these seconds only (full width = TX_WARN_SECS)
		int span = (last ? MIN(TX_WARN_SECS, total) : total);
		int16_t fillW = (int16_t)((span > 0) ? (((barW - 4) * CLAMP(remaining, 0, span)) / span) : 0);

		colour(last ? warnColour() : pal->mut);
		displayDrawRect(barX, barY, barW, barH, false);
		if (last)
		{
			hatch((barX + 2), (barY + 2), fillW, (barH - 4), warnColour());
		}
		else
		{
			fill((barX + 2), (barY + 2), fillW, (barH - 4), pal->tx);
		}

		buf[0] = '-';
		mmss(remaining, &buf[1], (sizeof(buf) - 1));
		if (last)
		{
			if (pal->cues)
			{
				inverseChip((DISPLAY_SIZE_X - 1), layout->rowY1, buf, warnColour(), pal->bg, true);
			}
			else
			{
				text((DISPLAY_SIZE_X - 2), layout->rowY1, buf, FONT_SIZE_1_BOLD, warnColour(), true);
			}
		}
		else
		{
			text((DISPLAY_SIZE_X - 2), layout->rowY1, buf, FONT_SIZE_1, pal->fg, true);
		}
	}

	if (digital)
	{
		uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));
		char contact[20];

		snprintf(buf, sizeof(buf), "%u", (unsigned)(PCorTG & 0xFFFFFF));
		keyValue(2, layout->rowY2, (((PCorTG >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), buf, pal->fg);
		snprintf(buf, sizeof(buf), "%u", (unsigned)trxGetDMRColourCode());
		keyValue(COL2_X, layout->rowY2, "CC", buf, pal->fg);
		snprintf(buf, sizeof(buf), "%d", (trxGetDMRTimeSlot() + 1));
		keyValue(COL3_X, layout->rowY2, "TS", buf, pal->fg);

		codeplugUtilConvertBufToString(currentContactData.name, contact, 16);
		if (nonVolatileSettings.overrideTG != 0)
		{
			uiUtilityBuildTgOrPCDisplayName(contact, sizeof(contact));
		}
		contact[16] = 0;
		text(2, layout->rowY3, contact, FONT_SIZE_1, pal->fg, false);
		text((DISPLAY_SIZE_X - 2), layout->rowY3, "SK1 LH", FONT_SIZE_1, pal->mut, true);
	}
	else
	{
		toneString(currentChannelData->txTone, buf, sizeof(buf));
		keyValue(2, layout->rowY2, "TXT", buf, ((currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE) ? pal->fg : pal->mut));
		keyValue(COL2_X, layout->rowY2, "BW", (trxGetBandwidthIs25kHz() ? "25k" : "12.5k"), pal->fg);
		text(2, layout->rowY3, "SK2 1750  0-9 DTMF", FONT_SIZE_1, pal->mut, false);
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

static void drawEmcomRows(const emcomLayout_t *layout, const emcomPositionDisplay_t *position)
{
	char buf[28];
	uint32_t now = ticksGetMillis();
	int16_t y1 = layout->rowY1;
	int16_t y2 = layout->rowY2;
	int16_t y3 = layout->rowY3;

#if defined(I2S_APRS_RX)
	uint32_t unread = aprsRxMessageUnreadCount();
#if defined(HAS_DMR_SMS)
	uint32_t smsUnread = dmrSmsUnreadCount();
#endif

	if (aprsRxGetBackground())
	{
		snprintf(buf, sizeof(buf), "%u stn", (unsigned)aprsRxStationCount());
		keyValue(2, y1, "APRS", buf, pal->fg);
	}
	else
	{
		keyValue(2, y1, "APRS", "off", pal->mut);
	}

#if defined(HAS_DMR_SMS)
	// unread DMR SMS in the same chip: "SMS 2" alone, "MSG 1+2" with APRS messages
	if (smsUnread)
	{
		if (unread)
		{
			snprintf(buf, sizeof(buf), "MSG %u+%u", (unsigned)(unread > 9U ? 9U : unread), (unsigned)(smsUnread > 9U ? 9U : smsUnread));
		}
		else
		{
			snprintf(buf, sizeof(buf), "SMS %u", (unsigned)smsUnread);
		}
		fill(72, (y1 + EMCOM_CHIP_TOP_OFFSET), (int16_t)((strlen(buf) * 6) + 4), EMCOM_CHIP_HEIGHT, pal->info);
		text(74, y1, buf, FONT_SIZE_1_BOLD, pal->bg, false);
	}
	else
#endif
	if (unread)
	{
		snprintf(buf, sizeof(buf), "MSG %u", (unsigned)unread);
		fill(72, (y1 + EMCOM_CHIP_TOP_OFFSET), (int16_t)((strlen(buf) * 6) + 4), EMCOM_CHIP_HEIGHT, pal->info);
		text(74, y1, buf, FONT_SIZE_1_BOLD, pal->bg, false);
	}
	else
	{
		keyValue(72, y1, "MSG", "0", pal->mut);
	}
#endif

	if (position->valid)
	{
		text((DISPLAY_SIZE_X - 2), y1, position->maidenhead, FONT_SIZE_1,
				((position->source == EMCOM_POSITION_GPS) ? pal->rx : pal->fg), true);
		keyValue(2, y2, "POS", position->positionText, pal->fg);
	}
	else
	{
		keyValue(2, y2, "POS", "no fix / location", pal->mut);
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
		keyValue(2, y3, "LH", buf, pal->fg);
	}
#if defined(I2S_APRS_RX)
	else if (aprsRxStationCount() > 0)
	{
		const aprsRxStation_t *st = aprsRxStationGet(0);
		char age[8];

		ageString((now - st->lastMs), age, sizeof(age));
		if (position->aprsDistanceVisible)
		{
			snprintf(buf, sizeof(buf), "%s %s %s", st->name, position->aprsDistance, age);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%s %s", st->name, age);
		}
		keyValue(2, y3, "LH", buf, pal->fg);
	}
#endif
}

static void drawFooter(emcomState_t st)
{
	char buf[24];
	const char *right = NULL;
	uint32_t rightCol = pal->barFg;
	bool alert = false; // SOS / TX OFF / TX: an inverse chip in the mono and contrast styles

	fill(0, FOOTER_Y, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - FOOTER_Y), pal->bar);

	if (uiDataGlobal.dateTimeSecs > 86400U)
	{
		struct tm utc;
		struct tm local;
		time_t_custom secs = uiDataGlobal.dateTimeSecs;
		static const char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

		gmtime_r_Custom(&secs, &utc);
		if ((nonVolatileSettings.timezone & 0x80) != 0)
		{
			// Options > Display > Clock: Local, or Local + Zulu
			int32_t offset = (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60));
			time_t_custom localSecs = (time_t_custom)((int64_t)secs + offset);

			gmtime_r_Custom(&localSecs, &local);
			if (settingsIsOptionBitSet(BIT_CLOCK_LOCAL_AND_ZULU))
			{
				snprintf(buf, sizeof(buf), "%02d:%02d/%02d:%02dZ %02d%s", local.tm_hour, local.tm_min,
						utc.tm_hour, utc.tm_min, local.tm_mday, months[CLAMP(local.tm_mon, 0, 11)]);
			}
			else
			{
				snprintf(buf, sizeof(buf), "%02d:%02d %02d%s", local.tm_hour, local.tm_min, local.tm_mday, months[CLAMP(local.tm_mon, 0, 11)]);
			}
		}
		else
		{
			snprintf(buf, sizeof(buf), "%02d:%02dZ %02d%s", utc.tm_hour, utc.tm_min, utc.tm_mday, months[CLAMP(utc.tm_mon, 0, 11)]);
		}
	}
	else
	{
		snprintf(buf, sizeof(buf), (((nonVolatileSettings.timezone & 0x80) == 0) ? "--:--Z" : (settingsIsOptionBitSet(BIT_CLOCK_LOCAL_AND_ZULU) ? "--:--/--:--Z" : "--:--")));
	}

	if (st == ST_SCAN)
	{
		snprintf(buf, sizeof(buf), "ANY KEY: STOP");
	}
	int16_t leftEnd = (int16_t)(2 + text(2, (FOOTER_Y + 2), buf, FONT_SIZE_1, pal->barFg, false)); // first free column after the clock

	if (sosOn)
	{
		right = "SOS";
		rightCol = pal->tx;
		alert = true;
	}
	else if (st == ST_SCAN)
	{
		right = "SCAN";
		rightCol = pal->scan;
	}
	else if (settingsIsOptionBitSet(BIT_TX_INHIBIT))
	{
		right = "TX OFF";
		rightCol = pal->tx;
		alert = true;
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
		alert = true;
	}
	else if (st == ST_RX)
	{
		right = "RX";
		rightCol = pal->rx;
	}

	int16_t r = (DISPLAY_SIZE_X - 2); // right edge of the next item

	if (right)
	{
		if (pal->cues && alert)
		{
			r = (int16_t)((DISPLAY_SIZE_X - 1) - inverseChip((DISPLAY_SIZE_X - 1), (FOOTER_Y + 2), right, rightCol, pal->bar, true) - 3); // SOS, TX OFF, TX
		}
		else
		{
			r -= (text(r, (FOOTER_Y + 2), right, FONT_SIZE_1_BOLD, rightCol, true) + 4);
		}
	}

	if (batteryTimeLeftMinutes() != BATT_RT_UNKNOWN)
	{
		// Fork: battery runtime estimate, next to the state indicator (RX, SCAN, locks...) whenever it fits
		// between it and the clock (the BLK chip, when shown, keeps its place)
		int16_t blkW = (blackoutActive() ? (int16_t)((3 * 6) + 4 + 2) : 0);

		batteryTimeLeftString(buf, sizeof(buf));
		if ((r - blkW - (int16_t)(strlen(buf) * 6)) > (leftEnd + 3))
		{
			r -= (text(r, (FOOTER_Y + 2), buf, FONT_SIZE_1, pal->barFg, true) + 4);
		}
	}

	if (blackoutActive())
	{
		// solid chip in every style: shape-distinct from the text items
		inverseChip((r + 1), (FOOTER_Y + 2), "BLK", pal->barFg, pal->bar, true);
	}
}

#if defined(I2S_APRS_RX)
// What the MSG chip shows: APRS unread messages, DMR SMS unread in the high half
static uint32_t emcomUnreadKey(void)
{
#if defined(HAS_DMR_SMS)
	return (aprsRxMessageUnreadCount() | (dmrSmsUnreadCount() << 16));
#else
	return aprsRxMessageUnreadCount();
#endif
}
#endif

static void rememberDrawn(emcomState_t st, const emcomMainInfo_t *info, emcomPanel_t panel,
		const emcomPositionDisplay_t *position, uint32_t renderedAt)
{
	drawn.state = (uint8_t)st;
	drawn.volume = (int8_t)visibleVolumeLevel();
	drawn.minute = (uiDataGlobal.dateTimeSecs / 60U);
#if defined(I2S_APRS_RX)
	drawn.unread = emcomUnreadKey();
	drawn.stations = aprsRxStationCount();
#endif
	drawn.gpsFix = gpsHasFix();
	drawn.sosPhase = (((renderedAt / 600U) & 1U) == 0U);
	drawn.rxDbm = (int16_t)trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	drawn.radioMode = (uint8_t)trxGetMode();
	drawn.panel = panel;
	drawn.isVFO = info->isVFO;
	drawn.zoneIndex = currentZone.NOT_IN_CODEPLUGDATA_indexNumber;
	drawn.channelIndex = (info->isVFO ? -1 : codeplugGetLastUsedChannelInCurrentZone());
	drawn.rxFreq = ((currentChannelData != NULL) ? currentChannelData->rxFreq : 0U);
	drawn.txFreq = ((currentChannelData != NULL) ? currentChannelData->txFreq : 0U);
	drawn.positionVisible = (st != ST_TX);
	drawn.positionValid = (drawn.positionVisible && position->valid);
	drawn.positionSource = (drawn.positionVisible ? position->source : EMCOM_POSITION_NONE);
	drawn.positionText[0] = 0;
	drawn.maidenhead[0] = 0;
	drawn.repeaterDistanceVisible = false;
	drawn.repeaterDistance[0] = 0;
	drawn.aprsDistanceVisible = false;
	drawn.aprsDistance[0] = 0;
	if (drawn.positionVisible)
	{
		if (position->valid)
		{
			snprintf(drawn.positionText, sizeof(drawn.positionText), "%s", position->positionText);
			snprintf(drawn.maidenhead, sizeof(drawn.maidenhead), "%s", position->maidenhead);
		}
		if (position->repeaterDistanceVisible)
		{
			drawn.repeaterDistanceVisible = true;
			snprintf(drawn.repeaterDistance, sizeof(drawn.repeaterDistance), "%s", position->repeaterDistance);
		}
		if (position->aprsDistanceVisible)
		{
			drawn.aprsDistanceVisible = true;
			snprintf(drawn.aprsDistance, sizeof(drawn.aprsDistance), "%s", position->aprsDistance);
		}
		drawn.lastPositionRendered = renderedAt;
	}
	drawn.distanceEnabled = settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE);
	drawn.lastFull = renderedAt;
	drawn.blackout = blackoutActive();
	drawn.valid = true;
}

void emcomRenderMain(const emcomMainInfo_t *info)
{
	emcomState_t st = currentState();
	emcomPanel_t panel;
	const emcomLayout_t *layout;
	emcomPosition_t position;
	emcomPositionDisplay_t positionDisplay;
	bool showDistanceRow;
	bool showChannelDistance;
	memset(&position, 0, sizeof(position));

	selectPalette();
	if (info->isTransmitting)
	{
		st = ST_TX;
	}
	panel = renderedPanel(info, st);
	showDistanceRow = channelDistanceRowVisible(info, st, panel);
	showChannelDistance = channelDistanceDisplayEligible(info, st, panel);
	layout = layoutForPanel(panel, showDistanceRow);
	lastRenderedPanel = panel;
	lastRenderedLayout = layout;
	if (panel == EMCOM_PANEL_TX)
	{
		lastTxLayout = layout;
	}
	if (st != ST_TX)
	{
		ownPositionInfo(&position);
	}
	buildPositionDisplay(&position, showChannelDistance, &positionDisplay);

	colour(pal->bg);
	displayFillRect(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, true);

	emcomRenderStatusBar(false);
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
	divider(EMCOM_DIV1_Y);

	if (st == ST_TX)
	{
		// TOT set: the TX screen counts the remaining time down, else the elapsed time up
		int t = ((info->txTimeSecs < 0) ? 0 : info->txTimeSecs);
		int remaining = ((currentChannelData->tot != 0) ? t : -1);
		int elapsed = ((remaining >= 0) ? ((int)(currentChannelData->tot * 15U) - t) : t);

		drawTxPanel(info, elapsed);
		divider(layout->lowerDividerY);
		drawTxRows(layout, remaining);
	}
	else if (panel == EMCOM_PANEL_DETAILS)
	{
		drawDetailsPanel();
	}
	else if (panel == EMCOM_PANEL_DMR_RX)
	{
		drawCallerPanel(layout);
	}
	else if (panel == EMCOM_PANEL_FM_RX)
	{
		drawRxPanel(showDistanceRow, &positionDisplay);
	}
	else
	{
		drawParams();
		if (showDistanceRow)
		{
			drawDistanceLine(&positionDisplay);
		}
	}

	if (st != ST_TX)
	{
		divider(layout->lowerDividerY);
		drawEmcomRows(layout, &positionDisplay);
	}
	drawFooter(st);

	displayThemeResetToDefault();
	displayRender();
	rememberDrawn(st, info, panel, &positionDisplay, ticksGetMillis());
}

bool emcomNeedsFullRedraw(uint32_t now)
{
	emcomState_t st = currentState();
	emcomPanel_t panel = currentPanelForRedraw(st);
	bool isVFO = (menuSystemGetRootMenuNumber() == UI_VFO_MODE);

	if (drawn.valid == false)
	{
		return true;
	}

	if (((uint8_t)st != drawn.state) || (panel != drawn.panel) || (visibleVolumeLevel() != drawn.volume) ||
			((uiDataGlobal.dateTimeSecs / 60U) != drawn.minute) || (gpsHasFix() != drawn.gpsFix) || (blackoutActive() != drawn.blackout))
	{
		return true;
	}

	// These are screen-content changes, not position changes, so they remain
	// immediate and are not subject to the position refresh interval.
	if ((isVFO != drawn.isVFO) || ((uint8_t)trxGetMode() != drawn.radioMode) ||
			(currentChannelData == NULL) || (currentChannelData->rxFreq != drawn.rxFreq) ||
			(currentChannelData->txFreq != drawn.txFreq) ||
			((isVFO == false) && ((currentZone.NOT_IN_CODEPLUGDATA_indexNumber != drawn.zoneIndex) ||
					(codeplugGetLastUsedChannelInCurrentZone() != drawn.channelIndex))) ||
			(settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) != drawn.distanceEnabled))
	{
		return true;
	}

#if defined(I2S_APRS_RX)
	if ((emcomUnreadKey() != drawn.unread) || (aprsRxStationCount() != drawn.stations))
	{
		return true;
	}
#endif

	// The position rows are not rendered on the TX screen. Avoid polling GPS
	// coordinates, calculating distances, or formatting strings while hidden.
	if (drawn.positionVisible)
	{
		emcomPosition_t position;
		ownPositionInfo(&position);

		// Validity and source changes affect the visible position immediately.
		if ((position.valid != drawn.positionValid) ||
				(position.valid && (position.source != drawn.positionSource)))
		{
			return true;
		}

		// Display-aware position and distance checks run at most once per second.
		// The cached values are the values from the last completed full render.
		if ((now - drawn.lastPositionRendered) >= EMCOM_POSITION_REFRESH_MS)
		{
			emcomPositionDisplay_t currentDisplay;

			buildPositionDisplay(&position, drawn.repeaterDistanceVisible, &currentDisplay);
			if (positionDisplayChanged(&currentDisplay))
			{
				return true;
			}
		}
	}

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

// --- SP7TEAM emblem: antenna mast with radio waves (boot and power-off screens) ---------
void emcomDrawEmblem(int16_t cx, int16_t ty, themeItem_t waves, themeItem_t mast, themeItem_t bg)
{
	displayThemeApply(waves, bg);
	for (int16_t r = 7; r <= 19; r += 6)
	{
		for (int16_t a = -40; a <= 40; a += 2) // arcs, +/-40 degrees around the horizontal, both sides
		{
			double rad = ((double)a * (M_PI / 180.0));
			int16_t dx = (int16_t)lround(cos(rad) * r);
			int16_t dy = (int16_t)lround(sin(rad) * r);

			displaySetPixel((cx + dx), (ty + dy), true);
			displaySetPixel((cx - dx), (ty + dy), true);
		}
	}

	displayThemeApply(mast, bg);
	displayFillCircle(cx, ty, 2, true);
	displayDrawFastVLine(cx, ty, 26, true);
	displayDrawLine(cx, (ty + 8), (cx - 7), (ty + 26), true);
	displayDrawLine(cx, (ty + 8), (cx + 7), (ty + 26), true);
	displayDrawFastHLine((cx - 4), (ty + 17), 9, true);
	displayDrawFastHLine((cx - 9), (ty + 26), 19, true);
	displayThemeResetToDefault();
}

// APRS beacon on air (PTT beacon / timed beacon): an inverse block over the panel, as the TX status bar
void emcomDrawTxBanner(const char *str)
{
	const emcomLayout_t *layout = ((lastRenderedPanel == EMCOM_PANEL_TX) ? lastRenderedLayout : lastTxLayout);

	selectPalette();
	fill(0, (EMCOM_DIV1_Y + 1), DISPLAY_SIZE_X, ((layout->lowerDividerY - EMCOM_DIV1_Y) - 1), pal->tx);
	textRaw((int16_t)((DISPLAY_SIZE_X - (int16_t)(strlen(str) * 16)) / 2), TX_TIMER_Y, str, FONT_SIZE_4, pal->txFg, false);
	displayThemeResetToDefault();
}

// --- message boxes / notifications ----------------------------------------------------------
#define ALERT_X           2
#define ALERT_W           (DISPLAY_SIZE_X - (2 * ALERT_X))
#define ALERT_STRIP_H     12
#define ALERT_GLYPH       15                                // glyph box, left of the text
#define ALERT_TEXT_X      (ALERT_X + ALERT_GLYPH + 8)
#define ALERT_TEXT_W      ((ALERT_X + ALERT_W - 3) - ALERT_TEXT_X)
#define ALERT_CHARS       (ALERT_TEXT_W / 8)                // 16 (8x16 font)
#define ALERT_MAX_LINES   4
#define ALERT_KEYBAR_H    11

static uint32_t alertColour(uiAlertKind_t kind)
{
	switch (kind)
	{
		case UI_ALERT_WARNING:
			return warnColour();
		case UI_ALERT_ERROR:
			return pal->tx;
		case UI_ALERT_QUESTION:
			return pal->fg;
		default:
			return pal->info;
	}
}

// 15 x 15 glyph at (x, y): the shape tells the kind, the sign inside in the background colour
static void alertGlyph(int16_t x, int16_t y, uiAlertKind_t kind, uint32_t rgb)
{
	colour(rgb);
	switch (kind)
	{
		case UI_ALERT_WARNING: // triangle, '!'
			displayFillTriangle((x + 7), y, x, (y + 14), (x + 14), (y + 14), false);
			fill((x + 6), (y + 5), 3, 5, pal->bg);
			fill((x + 6), (y + 11), 3, 2, pal->bg);
			break;
		case UI_ALERT_ERROR: // square, 'X'
			fill((x + 1), y, 13, 15, rgb);
			fill(x, (y + 1), 15, 13, rgb);
			colour(pal->bg);
			for (int16_t d = 0; d < 2; d++)
			{
				displayDrawLine((x + 4 + d), (y + 4), (x + 10), (y + 10 - d), false);
				displayDrawLine((x + 4), (y + 10 - d), (x + 10 - d), (y + 4), false);
			}
			break;
		case UI_ALERT_QUESTION: // diamond, '?'
			displayFillTriangle((x + 7), y, x, (y + 7), (x + 14), (y + 7), false);
			displayFillTriangle(x, (y + 7), (x + 14), (y + 7), (x + 7), (y + 14), false);
			textRaw((x + 5), (y + 4), "?", FONT_SIZE_1_BOLD, pal->bg, false);
			break;
		default: // info: disc, 'i'
			displayFillCircle((x + 7), (y + 7), 7, false);
			fill((x + 6), (y + 3), 3, 2, pal->bg);
			fill((x + 6), (y + 6), 3, 6, pal->bg);
			break;
	}
}

// Overlay card of height 'h': clears a full width band from the top of the first text row it covers to the
// bottom of the last one (no half glyphs of the screen below), returns the card top, centred in the band.
// EMCOM main screen: its own row boundaries; other screens: 16 px rows.
static int16_t alertBand(int16_t h)
{
	uint8_t rows[] =
	{
		12, 17, EMCOM_DIV1_Y, (uint8_t)lastRenderedLayout->lowerDividerY,
		(uint8_t)(lastRenderedLayout->rowY2 - 1), (uint8_t)(lastRenderedLayout->rowY3 - 1),
		(uint8_t)(lastRenderedLayout->rowY3 + FONT_SIZE_1_HEIGHT + 1), FOOTER_Y
	};
	size_t rowCount = (sizeof(rows) / sizeof(rows[0]));
	int16_t y = (int16_t)((DISPLAY_SIZE_Y - h) / 2);
	int16_t top = 0;
	int16_t bottom = DISPLAY_SIZE_Y;

	if (emcomScreenActive())
	{
		for (size_t i = 0; i < rowCount; i++)
		{
			if (rows[i] <= (y - 2))
			{
				top = rows[i];
			}
			else if ((rows[i] >= (y + h + 2)) && (bottom == DISPLAY_SIZE_Y))
			{
				bottom = rows[i];
			}
		}
	}
	else
	{
		top = (int16_t)(((y - 2) / 16) * 16);
		bottom = (int16_t)MIN(DISPLAY_SIZE_Y, ((((y + h + 2) + 15) / 16) * 16));
	}

	fill(0, top, DISPLAY_SIZE_X, (bottom - top), pal->bg);
	return (int16_t)(top + (((bottom - top) - h) / 2));
}

// Card frame + title strip (kind colour, title in capitals in the background colour); returns the colour
static uint32_t alertCard(uiAlertKind_t kind, const char *title, int16_t y, int16_t h)
{
	static const char *const words[] = { "INFO", "INFO", "WARNING", "ERROR", "CONFIRM" };
	uint32_t c = alertColour(kind);
	char buf[26];
	size_t n = 0;
	// warnings / errors (and every card in the contrast style) get a 2 px frame
	int16_t t = ((pal->large || (pal->cues && ((kind == UI_ALERT_WARNING) || (kind == UI_ALERT_ERROR)))) ? 2 : 1);

	if (title == NULL)
	{
		title = words[((kind <= UI_ALERT_QUESTION) ? kind : UI_ALERT_INFO)];
	}
	for (; title[n] && (n < (sizeof(buf) - 1)); n++)
	{
		char ch = title[n];
		buf[n] = (((ch >= 'a') && (ch <= 'z')) ? (char)(ch - ('a' - 'A')) : ch);
	}
	buf[n] = 0;

	fill((ALERT_X - 2), (y - 2), (ALERT_W + 4), (h + 4), pal->bg); // margin from the screen below
	fill(ALERT_X, y, ALERT_W, ALERT_STRIP_H, c);
	fill(ALERT_X, y, t, h, c);
	fill((ALERT_X + ALERT_W - t), y, t, h, c);
	fill(ALERT_X, (y + h - t), ALERT_W, t, c);
	textRaw((ALERT_X + 4), (y + 2), buf, FONT_SIZE_1_BOLD, pal->bg, false);
	return c;
}

// Splits 'message' into lines of at most ALERT_CHARS ('\n', else at the last space); returns the count
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
				cut = ALERT_CHARS; // one long word: hard break
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

void emcomDrawAlert(uiAlertKind_t kind, const char *title, const char *message, const char *keys, bool fullScreen)
{
	char lines[ALERT_MAX_LINES][ALERT_CHARS + 1];
	int count;
	int16_t y, h, bodyY, bodyH;
	uint32_t c;

	selectPalette();
	count = alertWrap(message, lines);

	if (fullScreen)
	{
		int16_t bottom = (DISPLAY_SIZE_Y - (keys ? ALERT_KEYBAR_H : 0));

		fill(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, pal->bg);
		y = 2;
		h = (bottom - 4);
		if (keys)
		{
			fill(0, bottom, DISPLAY_SIZE_X, ALERT_KEYBAR_H, pal->bar);
			text(3, (bottom + 2), keys, FONT_SIZE_1, pal->barFg, false);
		}
	}
	else
	{
		h = (int16_t)(ALERT_STRIP_H + (((count > 0) ? count : 1) * FONT_SIZE_3_HEIGHT) + 6);
		y = alertBand(h);
	}

	c = alertCard(kind, title, y, h);
	bodyY = (y + ALERT_STRIP_H);
	bodyH = (h - ALERT_STRIP_H - 2);

	alertGlyph((ALERT_X + 5), (int16_t)(bodyY + ((bodyH - ALERT_GLYPH) / 2)), kind, c);

	int16_t ly = (int16_t)(bodyY + ((bodyH - (count * FONT_SIZE_3_HEIGHT)) / 2));
	for (int i = 0; i < count; i++)
	{
		int16_t w = (int16_t)(strlen(lines[i]) * 8);

		textRaw((int16_t)(ALERT_TEXT_X + ((ALERT_TEXT_W - w) / 2)), ly, lines[i], FONT_SIZE_3, pal->fg, false);
		ly += FONT_SIZE_3_HEIGHT;
	}

	displayThemeResetToDefault();
}

void emcomDrawLevelAlert(const char *title, const char *value, int percent)
{
	int16_t h = (ALERT_STRIP_H + FONT_SIZE_3_HEIGHT + 8);
	int16_t y, ty;

	selectPalette();
	y = alertBand(h);
	ty = (y + ALERT_STRIP_H + 3);
	alertCard(UI_ALERT_INFO, title, y, h);

	if (percent < 0)
	{
		textRaw((int16_t)((DISPLAY_SIZE_X - (strlen(value) * 8)) / 2), ty, value, FONT_SIZE_3, pal->fg, false);
	}
	else
	{
		int16_t bx = (ALERT_X + 6);
		int16_t bw = 96;
		int16_t bh = 12;
		int16_t by = (ty + 2);

		colour(pal->fg);
		displayDrawRect(bx, by, bw, bh, false);
		fill((bx + 2), (by + 2), (int16_t)(((bw - 4) * CLAMP(percent, 0, 100)) / 100), (bh - 4), pal->info);
		textRaw((ALERT_X + ALERT_W - 5), ty, value, FONT_SIZE_3, pal->fg, true);
	}

	displayThemeResetToDefault();
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
	// Mono: grey shades, brightness ladder alert > good > info > warning > decoration (the
	// screens also say it in words / shapes), selected row in inverse video (white block)
	static const uint32_t mono[THEME_ITEM_MAX] =
	{
		[THEME_ITEM_FG_DEFAULT] = 0xFFFFFF, [THEME_ITEM_BG] = 0x000000, [THEME_ITEM_FG_DECORATION] = 0x8A8A8A,
		[THEME_ITEM_FG_TEXT_INPUT] = 0xFFFFFF, [THEME_ITEM_FG_SPLASHSCREEN] = 0xFFFFFF, [THEME_ITEM_BG_SPLASHSCREEN] = 0x000000,
		[THEME_ITEM_FG_NOTIFICATION] = 0xFFFFFF, [THEME_ITEM_FG_WARNING_NOTIFICATION] = 0xFFFFFF, [THEME_ITEM_FG_ERROR_NOTIFICATION] = 0xFFFFFF,
		[THEME_ITEM_BG_NOTIFICATION] = 0x1E1E1E, [THEME_ITEM_FG_MENU_NAME] = 0xF0F0F0, [THEME_ITEM_BG_MENU_NAME] = 0x1E1E1E,
		[THEME_ITEM_FG_MENU_ITEM] = 0xFFFFFF, [THEME_ITEM_BG_MENU_ITEM_SELECTED] = 0xFFFFFF, [THEME_ITEM_FG_OPTIONS_VALUE] = 0xBEBEBE,
		[THEME_ITEM_FG_HEADER_TEXT] = 0xF0F0F0, [THEME_ITEM_BG_HEADER_TEXT] = 0x1E1E1E, [THEME_ITEM_FG_RSSI_BAR] = 0xDCDCDC,
		[THEME_ITEM_FG_RSSI_BAR_S9P] = 0xA0A0A0, [THEME_ITEM_FG_CHANNEL_NAME] = 0xFFFFFF, [THEME_ITEM_FG_CHANNEL_CONTACT] = 0xFFFFFF,
		[THEME_ITEM_FG_CHANNEL_CONTACT_INFO] = 0xBEBEBE, [THEME_ITEM_FG_ZONE_NAME] = 0x8A8A8A, [THEME_ITEM_FG_RX_FREQ] = 0xBEBEBE,
		[THEME_ITEM_FG_TX_FREQ] = 0xFFFFFF, [THEME_ITEM_FG_CSS_SQL_VALUES] = 0x8A8A8A, [THEME_ITEM_FG_TX_COUNTER] = 0xFFFFFF,
		[THEME_ITEM_FG_POLAR_DRAWING] = 0x8A8A8A, [THEME_ITEM_FG_SATELLITE_COLOUR] = 0xDCDCDC, [THEME_ITEM_FG_GPS_NUMBER] = 0xFFFFFF,
		[THEME_ITEM_FG_GPS_COLOUR] = 0xFFFFFF, [THEME_ITEM_FG_BD_COLOUR] = 0xA0A0A0
	};
	// Contrast: white text, yellow accents / values / warnings, selected row as a yellow block
	static const uint32_t contrast[THEME_ITEM_MAX] =
	{
		[THEME_ITEM_FG_DEFAULT] = 0xFFFFFF, [THEME_ITEM_BG] = 0x000000, [THEME_ITEM_FG_DECORATION] = 0xFFFF00,
		[THEME_ITEM_FG_TEXT_INPUT] = 0xFFFFFF, [THEME_ITEM_FG_SPLASHSCREEN] = 0xFFFFFF, [THEME_ITEM_BG_SPLASHSCREEN] = 0x000000,
		[THEME_ITEM_FG_NOTIFICATION] = 0xFFFFFF, [THEME_ITEM_FG_WARNING_NOTIFICATION] = 0xFFFF00, [THEME_ITEM_FG_ERROR_NOTIFICATION] = 0xFFFF00,
		[THEME_ITEM_BG_NOTIFICATION] = 0x202020, [THEME_ITEM_FG_MENU_NAME] = 0xFFFFFF, [THEME_ITEM_BG_MENU_NAME] = 0x202020,
		[THEME_ITEM_FG_MENU_ITEM] = 0xFFFFFF, [THEME_ITEM_BG_MENU_ITEM_SELECTED] = 0xFFFF00, [THEME_ITEM_FG_OPTIONS_VALUE] = 0xFFFF00,
		[THEME_ITEM_FG_HEADER_TEXT] = 0xFFFFFF, [THEME_ITEM_BG_HEADER_TEXT] = 0x202020, [THEME_ITEM_FG_RSSI_BAR] = 0xFFFFFF,
		[THEME_ITEM_FG_RSSI_BAR_S9P] = 0xFFFF00, [THEME_ITEM_FG_CHANNEL_NAME] = 0xFFFFFF, [THEME_ITEM_FG_CHANNEL_CONTACT] = 0xFFFFFF,
		[THEME_ITEM_FG_CHANNEL_CONTACT_INFO] = 0xFFFF00, [THEME_ITEM_FG_ZONE_NAME] = 0xFFFF00, [THEME_ITEM_FG_RX_FREQ] = 0xFFFFFF,
		[THEME_ITEM_FG_TX_FREQ] = 0xFFFF00, [THEME_ITEM_FG_CSS_SQL_VALUES] = 0xFFFF00, [THEME_ITEM_FG_TX_COUNTER] = 0xFFFF00,
		[THEME_ITEM_FG_POLAR_DRAWING] = 0x808080, [THEME_ITEM_FG_SATELLITE_COLOUR] = 0xFFFFFF, [THEME_ITEM_FG_GPS_NUMBER] = 0xFFFFFF,
		[THEME_ITEM_FG_GPS_COLOUR] = 0xFFFFFF, [THEME_ITEM_FG_BD_COLOUR] = 0xFFFF00
	};
	emcomStyle_t style = emcomStyleGet();
	const uint32_t *table = ((style == EMCOM_STYLE_MONO) ? mono : ((style == EMCOM_STYLE_CONTRAST) ? contrast : day));

	for (int i = 0; i < THEME_ITEM_MAX; i++)
	{
		uint32_t c = table[i];

		if (night && (style == EMCOM_STYLE_MONO))
		{
			// the grey ladder in reds (white -> 0xD22A2A, as the night palette)
			uint32_t r = (((c & 0xFFU) * 210U) / 255U);
			c = ((r << 16) | ((r / 5U) << 8) | (r / 5U));
		}
		else if (night && (style == EMCOM_STYLE_CONTRAST))
		{
			// white -> amber, yellow -> red-orange, greys -> dark red
			uint32_t g = (c & 0xFFU);
			c = (((c == 0xFFFFFFU) || (i == THEME_ITEM_BG_MENU_ITEM_SELECTED)) ? 0xFFA000U : ((c == 0xFFFF00U) ? 0xFF4020U : ((g << 16) | ((g / 4U) << 8))));
		}
		else if (night)
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

// --- tool screens (menu theme items) ------------------------------------------------------
bool emcomUiLarge(void)
{
	return (emcomStyleGet() == EMCOM_STYLE_CONTRAST);
}

int16_t emcomUiText(int16_t x, int16_t y, const char *str, ucFont_t font, themeItem_t colour, bool alignRight)
{
	int16_t w;

	if ((font == FONT_SIZE_1) && emcomUiLarge())
	{
		font = FONT_SIZE_1_BOLD;
	}
	w = (int16_t)(strlen(str) * fontWidth(font));
	if (alignRight)
	{
		x -= w;
	}
	if (x < 0)
	{
		x = 0;
	}
	displayThemeApply(colour, THEME_ITEM_BG);
	displayPrintCore(x, y, str, font, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
	return w;
}

int16_t emcomUiChip(int16_t x, int16_t y, const char *str, themeItem_t colour, emcomChip_t style, bool alignRight)
{
	int16_t w = (int16_t)((strlen(str) * 6) + 4);

	if (alignRight)
	{
		x -= w;
	}
	displayThemeApply(colour, THEME_ITEM_BG);
	if (style == EMCOM_CHIP_SOLID)
	{
		displayFillRect(x, (y - 1), w, 9, false);
	}
	else
	{
		displayDrawRect(x, (y - 1), w, 9, false);
	}
	displayPrintCore((x + 2), y, str, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, (style == EMCOM_CHIP_SOLID));
	displayThemeResetToDefault();
	return w;
}

int16_t emcomUiKeyValue(int16_t x, int16_t y, const char *key, const char *value, themeItem_t colour)
{
	int16_t w = (int16_t)(emcomUiText(x, y, key, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, false) + 4);
	return (int16_t)(w + emcomUiText((x + w), y, value, FONT_SIZE_1, colour, false));
}

void emcomUiTitle(const char *title, const char *counter)
{
	char buf[SCREEN_LINE_BUFFER_SIZE + 4];
	int maxChars = (int)((DISPLAY_SIZE_X - 8 - (counter ? ((int16_t)(strlen(counter) * 6) + 6) : 0)) / 8); // title: 8 px font from x 7

	snprintf(buf, sizeof(buf), "%.*s", ((maxChars > 0) ? maxChars : 0), title); // translated titles can be long
	menuDisplayTitle(buf);
	if (counter)
	{
		emcomUiText((DISPLAY_SIZE_X - 3), 4, counter, FONT_SIZE_1, THEME_ITEM_FG_DECORATION, true);
	}
}

void emcomUiStrip(void)
{
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EMCOM_UI_STRIP_Y, DISPLAY_SIZE_X, EMCOM_UI_STRIP_H, false);
	displayThemeResetToDefault();
}

void emcomUiDivider(int16_t y)
{
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, y, DISPLAY_SIZE_X, true);
	displayThemeResetToDefault();
}

void emcomUiKeyBar(const char *keys)
{
	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, EMCOM_UI_KEYBAR_Y, DISPLAY_SIZE_X, EMCOM_UI_KEYBAR_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (EMCOM_UI_KEYBAR_Y + 2), keys, (emcomUiLarge() ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
}

void emcomUiConfirmBar(const char *question)
{
	int16_t x;

	emcomUiKeyBar("");
	x = (int16_t)(2 + emcomUiChip(2, (EMCOM_UI_KEYBAR_Y + 2), question, THEME_ITEM_FG_TX_COUNTER, EMCOM_CHIP_SOLID, false) + 5);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(x, (EMCOM_UI_KEYBAR_Y + 2), (((x + (15 * 6)) <= DISPLAY_SIZE_X) ? "GRN YES  ANY NO" : "GRN YES ANY NO"),
			(emcomUiLarge() ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
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
			// the busy-channel wait blocks the UI for up to 2 s: do not retry too often
			sosNextMs = (ticksGetMillis() + SOS_BUSY_RETRY_MS);
			break;
		default:
			sosNextMs = (ticksGetMillis() + 5000U); // TX already in progress: retry soon
			break;
	}
}

// Screens where an SOS beacon may go out: the main screens and the field / info screens
// a person in distress plausibly looks at. Not on the TX, satellite (Doppler / uplink)
// or SSTV screens, CPS / hotspot, calibration, power off, nor while a text is edited.
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
#if defined(HAS_APRS_RX_MENU)
		case MENU_POSITION:
		case MENU_PACE:
		case MENU_COVERAGE:
		case MENU_RADIO_CHECK:
		case MENU_NET_CONTROL:
		case MENU_WAYPOINTS:
		case MENU_SKED:
		case MENU_DECODERS:
		case MENU_DMR_SMS:
		case MENU_EVENT_LOG:
#endif
			return true;
#if defined(HAS_APRS_RX_MENU)
		case MENU_APRS_RX:
			return (uiTextEditIsActive() == false); // not while a message is typed
#endif
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
		// once per held back beacon, and not over screens that own the display
		if ((sosPauseShown == false) && (menu != UI_CPS) && (menu != UI_HOTSPOT_MODE) && (menu != UI_POWER_OFF) &&
				(menu != UI_SPLASH_SCREEN) && (menu != MENU_CALIBRATION) && (menu != UI_TX_SCREEN))
		{
			sosPauseShown = true;
			uiNotificationShowKind(UI_ALERT_WARNING, NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2000U, "SOS PAUSED", false);
		}
		return;
	}

	sosPauseShown = false;
#if defined(I2S_APRS_RX)
	i2sAprsRxStop(); // restarted by aprsRxTick() after the transmission
#endif
	sosSend();
}

#endif // HAS_EMCOM_UI
