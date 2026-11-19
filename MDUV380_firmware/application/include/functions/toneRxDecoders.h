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

#endif
