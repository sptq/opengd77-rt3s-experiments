/*
 * Fork: DTMF and CW (Morse) decoders on the 8 kHz receive audio.
 *
 * Hardware independent (also compiled on the host for the offline tests):
 * feed 8 kHz signed 16-bit samples, the decoded characters are delivered to a
 * callback (on the radio from the I2S DMA interrupt).
 *
 * DTMF: Goertzel filters on the 8 standard tones, 205 sample blocks (25.6 ms),
 *       energy ratio + twist checks, a digit needs 2 good blocks and a pause.
 * CW:   Goertzel bank 400..1200 Hz (5 ms blocks), the strongest bin is tracked,
 *       adaptive key threshold, adaptive dot/dash timing (about 5..40 WPM).
 * STQC: Digitex / Platan selective call (fire brigade sirens, SW-xx CB selcall), receive only.
 *       Five tones 980 / 1197 / 1446 / 1795 / 2105 Hz = symbols 0..4, ~100 ms each, back to back;
 *       a call is usually 7 tones (address), ~200 ms pause, 8 tones (command). Symbol 4 repeats the
 *       previous digit, the digits are a base 4 number (see README_STQC of sq5bpf/multimon-ng-stqc).
 *       Goertzel on the 5 tones, 10 ms blocks; a symbol needs 40 ms of a clean tone, a group ends after
 *       120 ms without a tone and is delivered when it has 5..8 symbols.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_TONE_RX_DECODERS_H_
#define _OPENGD77_TONE_RX_DECODERS_H_

#include <stdint.h>
#include <stdbool.h>

typedef void (*toneRxCharCallback_t)(char c);

void dtmfRxInit(toneRxCharCallback_t callback);
void dtmfRxProcess(const int16_t *samples, uint32_t count, uint32_t stride);

void cwRxInit(toneRxCharCallback_t callback);
void cwRxProcess(const int16_t *samples, uint32_t count, uint32_t stride);
uint8_t cwRxGetWpm(void);       // 0 when unknown
uint16_t cwRxGetToneHz(void);   // tracked tone, 0 when unknown
bool cwRxIsKeyed(void);

#define STQC_MIN_SYMBOLS  5U
#define STQC_MAX_SYMBOLS  8U
// symbols: 0..4 as received (4 = repeat), len: STQC_MIN_SYMBOLS..STQC_MAX_SYMBOLS
typedef void (*stqcRxGroupCallback_t)(const uint8_t *symbols, uint8_t len);

void stqcRxInit(stqcRxGroupCallback_t callback);
void stqcRxProcess(const int16_t *samples, uint32_t count, uint32_t stride);
// The number carried by a group: repeats (4) resolved, base 4, most significant first
uint16_t stqcSymbolsToValue(const uint8_t *symbols, uint8_t len);
// The symbols a value is sent as, as text ("40103034" for 1231 in 8 symbols); buf >= len + 1
void stqcValueToSymbolText(uint16_t value, uint8_t len, char *buf);

#endif
