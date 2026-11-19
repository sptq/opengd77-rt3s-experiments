/*
 * Fork: APRS RX screen (main menu > APRS RX), by Marcin SP7TQ. APRS frames,
 * stations, radar and messages, plus the DTMF and CW decoders, all fed by the
 * FM RX audio (functions/aprsRx.c). Moved out of the RSSI screen, where '9'
 * still opens it.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <math.h>
#include "user_interface/uiGlobals.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/sound.h"
#include "interfaces/i2s.h"
#include "functions/aprsRx.h"
#include "functions/toneRxDecoders.h"
#include "functions/ticks.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

#if defined(HAS_APRS_RX_MENU) && defined(I2S_APRS_RX)
// Fork: APRS RX monitor (main menu > APRS RX, or '9' on the RSSI screen):
// status line + one of the views, cycled with '#': received frames (UP/DOWN select,
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

// Line with left and right parts, the left one truncated. Selected: full width bar with a
// marker (as in the menus). Left part bold, right part muted.
static void aprsMonPrintRow(int16_t y, char *left, const char *right, bool selected)
{
	size_t maxLeft = (APRS_MON_CHARS - strlen(right) - 1);

	if (strlen(left) > maxLeft)
	{
		left[maxLeft] = 0;
	}

	if (selected)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRect(0, y, DISPLAY_SIZE_X, FONT_SIZE_1_HEIGHT, false);
		displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG);
		displayFillRect(0, (y + 2), 2, (FONT_SIZE_1_HEIGHT - 4), false);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(4, y, left, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
		displayPrintCore(0, y, right, FONT_SIZE_1, TEXT_ALIGN_RIGHT, true);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCore(4, y, left, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(0, y, right, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
	}
	displayThemeResetToDefault();
}

// Empty view: a heading, a hint, the keys
static void aprsMonDrawHint(const char *line1, const char *line2)
{
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintCentered((APRS_MON_LIST_Y + 22), line1, FONT_SIZE_3);
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	displayPrintCentered((APRS_MON_LIST_Y + 44), line2, FONT_SIZE_1);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(20, (APRS_MON_LIST_Y + 60), (DISPLAY_SIZE_X - 40), true);
	displayPrintCentered((APRS_MON_LIST_Y + 66), "# VIEW  0 CLEAR  9 EXIT", FONT_SIZE_1);
	displayThemeResetToDefault();
}

// Small filled field with the text in the background colour ("chip")
static int16_t aprsMonChip(int16_t x, int16_t y, const char *text, themeItem_t colour)
{
	int16_t w = (int16_t)((strlen(text) * 6) + 4);

	displayThemeApply(colour, THEME_ITEM_BG);
	displayFillRect(x, (y - 1), w, (FONT_SIZE_1_HEIGHT + 1), false);
	displayPrintCore((x + 2), y, text, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	displayThemeResetToDefault();
	return w;
}

// Bottom bar (as the EMCOM footer): text on the left, optional chip on the right
#define APRS_MON_FOOTER_H   10
static void aprsMonFooter(const char *text, const char *chip, themeItem_t chipColour)
{
	int16_t y = (DISPLAY_SIZE_Y - APRS_MON_FOOTER_H);

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, y, DISPLAY_SIZE_X, APRS_MON_FOOTER_H, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (y + 1), text, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	if (chip)
	{
		aprsMonChip((DISPLAY_SIZE_X - 2 - (int16_t)((strlen(chip) * 6) + 4)), (y + 1), chip, chipColour);
	}
	displayThemeResetToDefault();
}

// "KEY value" with the key muted
static int16_t aprsMonKeyValue(int16_t x, int16_t y, const char *key, const char *value, themeItem_t valueColour)
{
	int16_t w = (int16_t)((strlen(key) * 6) + 3);

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCore(x, y, key, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeApply(valueColour, THEME_ITEM_BG);
	displayPrintCore((x + w), y, value, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
	return (int16_t)(w + (strlen(value) * 6));
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

		displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
		for (uint32_t l = 0; l < 2; l++)
		{
			aprsMonSlice(text, n, (l * APRS_MON_CHARS), line);
			displayPrintAt(4, (y + ((l + 1) * FONT_SIZE_1_HEIGHT)), line, FONT_SIZE_1);
		}
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine(0, (y + APRS_MON_BLOCK_H - 1), DISPLAY_SIZE_X, true);
		displayThemeResetToDefault();
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
#define APRS_MON_STATION_ROWS   ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - APRS_MON_FOOTER_H - 1) / (FONT_SIZE_1_HEIGHT + 1))

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
		aprsMonPrintRow((APRS_MON_LIST_Y + (r * (FONT_SIZE_1_HEIGHT + 1))), left, right, ((first + r) == sel));
	}

	// Details of the selected station
	const aprsRxStation_t *st = aprsRxStationGet(sel);
	if (st)
	{
		if (st->speedKmh >= 0)
		{
			snprintf(left, sizeof(left), "%ukm/h %03u  x%u", (unsigned)st->speedKmh, (unsigned)st->courseDeg, (unsigned)st->count);
		}
		else
		{
			snprintf(left, sizeof(left), "x%u%s", (unsigned)st->count, (st->hasPos ? (hasOwn ? "" : "  no own pos") : "  no position"));
		}
		aprsMonFooter(left, "GRN MSG", THEME_ITEM_FG_RX_FREQ);
	}
}

// --- Radar view (north up, own position in the centre) --------------------------------
#define RADAR_R    44
#define RADAR_CX   (RADAR_R + 3)
#define RADAR_CY   (APRS_MON_LIST_Y + 9 + RADAR_R)
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

	// Rings, dotted cross, north, own position
	displayThemeApply(THEME_ITEM_FG_POLAR_DRAWING, THEME_ITEM_BG);
	displayDrawCircle(RADAR_CX, RADAR_CY, RADAR_R, true);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawCircle(RADAR_CX, RADAR_CY, (RADAR_R / 2), true);
	for (int16_t d = -RADAR_R + 2; d <= (RADAR_R - 2); d += 3)
	{
		displaySetPixel((RADAR_CX + d), RADAR_CY, true);
		displaySetPixel(RADAR_CX, (RADAR_CY + d), true);
	}
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintAt((RADAR_CX - 2), (RADAR_CY - RADAR_R - 8), "N", FONT_SIZE_1_BOLD);
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	displayFillTriangle(RADAR_CX, (RADAR_CY - 4), (RADAR_CX - 3), (RADAR_CY + 3), (RADAR_CX + 3), (RADAR_CY + 3), true);
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
			displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
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

		displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		displayDrawRect((x - 3), (y - 3), 7, 7, true); // waypoint: square
		displayThemeResetToDefault();
	}
#endif

	// Selected station panel
	int16_t ty = (APRS_MON_LIST_Y + 1);
	if (selSt)
	{
		char dist[12];
		double d = aprsRxDistanceKm(myLat, myLon, selSt->lat, selSt->lon);

		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		snprintf(buf, sizeof(buf), "%s", selSt->name);
		buf[10] = 0;
		displayPrintAt(RADAR_TX, ty, buf, FONT_SIZE_1_BOLD);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine(RADAR_TX, (ty + 9), (DISPLAY_SIZE_X - RADAR_TX - 2), true);
		displayThemeResetToDefault();

		aprsRxFormatDistance(d, dist, sizeof(dist));
		aprsMonKeyValue(RADAR_TX, (ty + 13), "DST", dist, THEME_ITEM_FG_DEFAULT);
		snprintf(buf, sizeof(buf), "%03u", (unsigned)(aprsRxBearingDeg(myLat, myLon, selSt->lat, selSt->lon) + 0.5) % 360U);
		aprsMonKeyValue(RADAR_TX, (ty + 23), "BRG", buf, THEME_ITEM_FG_DEFAULT);
		aprsMonAge((now - selSt->lastMs), age, sizeof(age));
		aprsMonKeyValue(RADAR_TX, (ty + 33), "AGE", age, THEME_ITEM_FG_DEFAULT);
		if (selSt->speedKmh >= 0)
		{
			snprintf(buf, sizeof(buf), "%u", (unsigned)selSt->speedKmh);
			aprsMonKeyValue(RADAR_TX, (ty + 43), "KMH", buf, THEME_ITEM_FG_DEFAULT);
		}
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintAt(RADAR_TX, ty, "no position", FONT_SIZE_1);
		displayPrintAt(RADAR_TX, (ty + 10), "heard yet", FONT_SIZE_1);
		displayThemeResetToDefault();
	}

	char rng[12];
	aprsRxFormatDistance(range, rng, sizeof(rng));
	aprsMonKeyValue(RADAR_TX, (DISPLAY_SIZE_Y - FONT_SIZE_1_HEIGHT - 1), "R", rng, THEME_ITEM_FG_RX_FREQ);
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

		static const themeItem_t statusColour[] =
		{
			THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_FG_RSSI_BAR,
			THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_FG_TX_COUNTER
		};
		bool out = (m->kind >= APRS_MSG_OUT_PENDING);
		bool selected = ((first + b) == aprsMonMsgSelected);

		aprsMonAge((now - m->timeMs), age, sizeof(age));
		snprintf(line, sizeof(line), "  %s", m->peer);
		aprsMonPrintRow(y, line, age, selected);

		// direction marker: in (green), out (blue), bulletin (amber)
		aprsMonChip(3, y, (out ? ">" : ((m->kind == APRS_MSG_IN_BULLETIN) ? "B" : "<")),
				(out ? THEME_ITEM_FG_RX_FREQ : ((m->kind == APRS_MSG_IN_BULLETIN) ? THEME_ITEM_FG_RSSI_BAR_S9P : THEME_ITEM_FG_RSSI_BAR)));

		if (status[m->kind][0])
		{
			if (m->kind == APRS_MSG_OUT_PENDING)
			{
				snprintf(right, sizeof(right), "%s%u", status[m->kind], (unsigned)m->tries);
			}
			else
			{
				snprintf(right, sizeof(right), "%s", status[m->kind]);
			}
			aprsMonChip((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(age) * 6) - 4 - (int16_t)((strlen(right) * 6) + 4)), y, right, statusColour[m->kind]);
		}

		uint32_t n = strlen(m->text);
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		for (uint32_t l = 0; l < 2; l++)
		{
			aprsMonSlice(m->text, n, (l * APRS_MON_CHARS), line);
			displayPrintAt(4, (y + ((l + 1) * FONT_SIZE_1_HEIGHT)), line, FONT_SIZE_1);
		}
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawFastHLine(0, (y + APRS_MON_BLOCK_H - 1), DISPLAY_SIZE_X, true);
		displayThemeResetToDefault();
	}
}

// Reply / new message: canned texts
static void aprsMonDrawReply(void)
{
	char line[APRS_MON_CHARS + 1];
	uint32_t rows = ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - APRS_MON_FOOTER_H - FONT_SIZE_1_HEIGHT - 2) / FONT_SIZE_1_HEIGHT);
	uint32_t first = ((aprsMonReplyIndex >= rows) ? (aprsMonReplyIndex - rows + 1) : 0);

	aprsMonKeyValue(4, APRS_MON_LIST_Y, "TO", aprsMonReplyTo, THEME_ITEM_FG_DEFAULT);

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

	aprsMonFooter("GRN SEND  RED CANCEL", NULL, THEME_ITEM_FG_DEFAULT);
}

// --- DTMF / CW views -----------------------------------------------------------------
static void aprsMonDrawToneText(bool cw)
{
	char text[APRS_RX_TONE_TEXT_LEN + 1];
	char line[24];
	uint32_t perLine = (DISPLAY_SIZE_X / 8); // FONT_SIZE_3: 8 px wide
	uint32_t lines = ((DISPLAY_SIZE_Y - APRS_MON_LIST_Y - APRS_MON_FOOTER_H - 2) / FONT_SIZE_3_HEIGHT);
	uint32_t len = aprsRxToneText(cw, text, sizeof(text), NULL);

	if (cw)
	{
		uint8_t wpm = cwRxGetWpm();

		if (wpm)
		{
			snprintf(line, sizeof(line), "%u WPM  %u Hz", (unsigned)wpm, (unsigned)cwRxGetToneHz());
		}
		else
		{
			snprintf(line, sizeof(line), "waiting for CW");
		}
		aprsMonFooter(line, (cwRxIsKeyed() ? "KEY" : NULL), THEME_ITEM_FG_RSSI_BAR);
	}
	else
	{
		aprsMonFooter((len ? "0 CLEAR" : "waiting for DTMF"), NULL, THEME_ITEM_FG_DEFAULT);
	}
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);

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
	displayThemeResetToDefault();
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

	// Status strip: state chip, audio level, frames OK / errors, unread messages or dBm
	int16_t sy = (MENU_HEADER_HEIGHT + 2);
	int16_t x = 2;

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (APRS_MON_LIST_Y - 2 - MENU_HEADER_HEIGHT), false);

	switch (st.state)
	{
		case APRS_RX_STATE_LISTENING:
			x += aprsMonChip(x, sy, "RX", THEME_ITEM_FG_RSSI_BAR) + 4;
			break;
		case APRS_RX_STATE_NO_CLOCK:
			x += aprsMonChip(x, sy, "NO CLK", THEME_ITEM_FG_TX_COUNTER) + 4;
			break;
		default:
			x += aprsMonChip(x, sy, ((trxGetMode() != RADIO_MODE_ANALOG) ? "FM ONLY" : "PAUSE"), THEME_ITEM_FG_RSSI_BAR_S9P) + 4;
			break;
	}

	if (st.state == APRS_RX_STATE_LISTENING)
	{
		// audio level, 20 px
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
		displayDrawRect(x, (sy + 1), 22, 6, true);
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG_HEADER_TEXT);
		displayFillRect((x + 1), (sy + 2), (int16_t)((st.levelPercent * 20U) / 100U), 4, false);
		x += 26;
	}

	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	snprintf(line, sizeof(line), "OK%u E%u", (unsigned)st.framesOk, (unsigned)st.crcErrors);
	displayPrintCore(x, sy, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	uint32_t unread = aprsRxMessageUnreadCount();
	if (aprsMonRawView)
	{
		snprintf(line, sizeof(line), "%u/%u", (unsigned)(aprsMonSelected + 1), (unsigned)aprsRxHistoryCount());
		displayPrintCore((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(line) * 6)), sy, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else if (unread)
	{
		snprintf(line, sizeof(line), "MSG %u", (unsigned)unread);
		aprsMonChip((DISPLAY_SIZE_X - 2 - (int16_t)((strlen(line) * 6) + 4)), sy, line, THEME_ITEM_FG_RX_FREQ);
	}
	else
	{
		snprintf(line, sizeof(line), "%ddBm", trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_HEADER_TEXT);
		displayPrintCore((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(line) * 6)), sy, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, (APRS_MON_LIST_Y - 2), DISPLAY_SIZE_X, true);
	displayThemeResetToDefault();

	// view position in the title bar
	if ((aprsMonReplyActive == false) && (aprsMonRawView == false))
	{
		snprintf(line, sizeof(line), "%d/%d", (int)(aprsMonView + 1), (int)APRS_MON_VIEW_MAX);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_MENU_NAME);
		displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(line) * 6)), 4, line, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		displayThemeResetToDefault();
	}

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
		menuSystemPopPreviousMenu();
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

menuStatus_t menuAPRSRx(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		aprsMonSetEnabled(true);
		aprsMonDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			aprsMonDraw();
		}
		else if ((ev->events & KEY_EVENT) && (aprsMonHandleEvent(ev) == false))
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				aprsMonSetEnabled(false);
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
		}
	}

	if (menuSystemGetCurrentMenuNumber() == MENU_APRS_RX) // still here (not left by a key above)
	{
		aprsMonUpdate(ev->time);
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuAPRSRx(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
