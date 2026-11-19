/*
 * Fork: "EMCOM" main screen style (channel / VFO / TX), by Marcin SP7TQ.
 *
 * A layout modelled on professional / military radios: fixed positions,
 * left aligned, channel number first, radio state shown by colour and by a
 * word (RX green, TX red, SCAN amber, SOS flashing red), UTC clock and
 * position always visible. The classic OpenGD77 screen stays available
 * (Options > Display > Screen style) and is still used for the special
 * modes (VFO sweep, dual watch, frequency entry, channel details).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_UI_EMCOM_H_
#define _OPENGD77_UI_EMCOM_H_

#include <stdint.h>
#include <stdbool.h>

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

bool emcomStyleEnabled(void);       // the setting
void emcomApplyStyle(void);         // flat boxes + default colours for the whole UI (boot, setting change)
bool emcomScreenActive(void);       // the setting + a VFO / channel / TX screen that uses it now
void emcomSetVfoAllowed(bool allowed); // VFO mode: false in sweep / dual watch / frequency entry
void emcomSetChannelAllowed(bool allowed); // channel mode: false for the channel details / direct channel entry

void emcomRenderMain(const emcomMainInfo_t *info); // whole screen, rendered
void emcomRenderStatusBar(void);                   // rows 0..10 (no render)
void emcomDrawBar(int barWidth, bool isTx);        // S-meter / mic level, y 12..14 (no render)
void emcomDrawRssi(int dBm);                       // S-meter from dBm (no render)
bool emcomNeedsFullRedraw(uint32_t now);           // state, clock, messages... changed

// Theme items (native colours) matching the EMCOM screen: dark by day, red at night
void emcomThemePreset(bool night, uint16_t *items);

// SOS: APRS emergency beacons + banner
void emcomSosToggle(void);
void emcomSosRequest(void);                        // SK1 + 9 / quickkey: confirm with a second press
bool emcomSosActive(void);
void emcomSosTick(void);                           // main loop

#endif
#endif
