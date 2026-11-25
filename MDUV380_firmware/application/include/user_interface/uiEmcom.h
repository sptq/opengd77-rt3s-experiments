/*
 * Fork: "EMCOM" main screen style (channel / VFO / TX), by Marcin SP7TQ.
 *
 * A layout modelled on professional / military radios: fixed positions,
 * left aligned, channel number first, radio state shown by colour and by a
 * word (RX green, TX red, SCAN amber, SOS flashing red), UTC clock and
 * position always visible. The classic OpenGD77 screen stays available
 * (Options > Display > Screen style) and is still used for the special
 * modes (VFO sweep, dual watch and frequency entry); channel details remains
 * in the compact EMCOM layout.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_UI_EMCOM_H_
#define _OPENGD77_UI_EMCOM_H_

#include <stdint.h>
#include <stdbool.h>
#if defined(PLATFORM_MD9600)
#include "hardware/ST7567.h"
#else
#include "hardware/HX8353E.h"
#endif
#include "user_interface/uiGlobals.h"

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
#define HAS_EMCOM_UI
#endif

#if defined(HAS_EMCOM_UI)

typedef struct
{
	bool        isVFO;
	const char *zoneName;       // channel mode
	int         channelNumber;  // channel mode, 1-based
	int         channelCount;   // channel mode, 0 = unknown
	char        vfoLetter;      // VFO mode
	bool        isTransmitting;
	int         txTimeSecs;
	bool        showDetails;    // SK1 held: RX / TX frequencies and tones
	int         gotoChannel;    // channel mode: number being typed, 0 = none
	const char *freqEntry;      // VFO: FreqEnter digits ('-' = not typed yet), NULL = none
	int         freqEntryIndex; // VFO: next digit
	bool        entryTx;        // VFO: the TX frequency is selected / entered
} emcomMainInfo_t;

// Options > Display > Screen. Mono and contrast are EMCOM variants (emcomStyleEnabled() is true)
typedef enum
{
	EMCOM_STYLE_CLASSIC = 0,
	EMCOM_STYLE_EMCOM,
	EMCOM_STYLE_MONO,      // colour-blind safe: grey shades, state by shape / inverse video / words
	EMCOM_STYLE_CONTRAST,  // low vision: white / yellow on black, bold text, larger frequency, thick S-meter
	EMCOM_STYLE_COUNT
} emcomStyle_t;

emcomStyle_t emcomStyleGet(void);
void emcomStyleSet(emcomStyle_t style); // setting only, call emcomApplyStyle() afterwards
const char *emcomStyleName(emcomStyle_t style);
bool emcomStyleEnabled(void);       // the setting: any EMCOM variant
bool emcomShapeCues(void);          // mono / contrast: state also shown by shape (stripes, inverse chips)
void emcomApplyStyle(void);         // flat boxes + default colours for the whole UI (boot, setting change)
bool emcomScreenActive(void);       // the setting + a VFO / channel / TX screen that uses it now
void emcomSetVfoAllowed(bool allowed); // VFO mode: false in sweep / dual watch / frequency entry
void emcomSetChannelAllowed(bool allowed); // channel mode: false for the channel details / direct channel entry

void emcomRenderMain(const emcomMainInfo_t *info); // whole screen, rendered
void emcomRenderStatusBar(bool forceBatteryDisplay); // rows 0..10 (no render)
void emcomDrawBar(int barWidth, bool isTx);        // S-meter / mic level, y 12..14 (no render)
void emcomDrawRssi(int dBm);                       // S-meter from dBm (no render)
bool emcomNeedsFullRedraw(uint32_t now);           // state, clock, messages... changed

// SP7TEAM emblem (antenna mast + waves), mast top at (cx, ty), 26 px high
void emcomDrawEmblem(int16_t cx, int16_t ty, themeItem_t waves, themeItem_t mast, themeItem_t bg);

// Theme items (native colours) matching the EMCOM screen: dark by day, red at night
void emcomThemePreset(bool night, uint16_t *items);

void emcomDrawTxBanner(const char *str);         // "APRS Tx" over the panel while a beacon is sent (no render)

// Message boxes, notifications and the TX stop screen (uiAlertKind_t in uiGlobals.h): a card with a
// title strip (the kind word, or 'title'), a glyph whose shape tells the kind (info: disc, warning:
// triangle, error: square, question: diamond) and the message ('\n' separated, long lines wrapped).
// fullScreen: the whole screen (keys: key bar at the bottom, NULL = none), else a card centred over
// the current screen content. No render.
void emcomDrawAlert(uiAlertKind_t kind, const char *title, const char *message, const char *keys, bool fullScreen);
// Notification with a value and an optional level bar (percent < 0: none), e.g. squelch, volume, power
void emcomDrawLevelAlert(const char *title, const char *value, int percent);

// --- EMCOM style of the tool screens (PACE, radio check, event log, coverage, position, SSTV) ----
// Drawn with the menu theme items (emcomThemePreset()), as the GPS / satellite / APRS screens:
// title bar (menuDisplayTitle) with a counter on the right, a status strip of chips, the body and
// a key bar with the real keys. No render, no static data.
#define EMCOM_UI_STRIP_Y    MENU_HEADER_HEIGHT
#define EMCOM_UI_STRIP_H    11
#define EMCOM_UI_BODY_Y     (EMCOM_UI_STRIP_Y + EMCOM_UI_STRIP_H + 2)
#define EMCOM_UI_KEYBAR_H   11
#define EMCOM_UI_KEYBAR_Y   (DISPLAY_SIZE_Y - EMCOM_UI_KEYBAR_H)

typedef enum
{
	EMCOM_CHIP_SOLID = 0,  // text in the background colour on a 'colour' block
	EMCOM_CHIP_OUTLINE     // 'colour' frame and text (an off / idle state, or a chip on a selected row)
} emcomChip_t;

bool emcomUiLarge(void);   // contrast style: bold small text, bigger values
// Text at (x, y) (x: the right edge when alignRight); the contrast style draws FONT_SIZE_1 bold. Returns the width.
int16_t emcomUiText(int16_t x, int16_t y, const char *str, ucFont_t font, themeItem_t colour, bool alignRight);
// Small chip around a FONT_SIZE_1 text row at y (10 px high, y - 1 .. y + 8). Returns the width.
int16_t emcomUiChip(int16_t x, int16_t y, const char *str, themeItem_t colour, emcomChip_t style, bool alignRight);
int16_t emcomUiKeyValue(int16_t x, int16_t y, const char *key, const char *value, themeItem_t colour); // "KEY value"
void emcomUiTitle(const char *title, const char *counter); // counter: "3/16", NULL = none
void emcomUiStrip(void);                                   // status strip background (text rows at EMCOM_UI_STRIP_Y + 2)
void emcomUiDivider(int16_t y);
void emcomUiKeyBar(const char *keys);
void emcomUiConfirmBar(const char *question);              // "<question> GRN YES  ANY NO" (decided on the key release)

// SOS: APRS emergency beacons + banner
void emcomSosToggle(void);
void emcomSosRequest(void);                        // SK1 + 9 / quickkey: confirm with a second press
bool emcomSosActive(void);
void emcomSosTick(void);                           // main loop

#endif
#endif
