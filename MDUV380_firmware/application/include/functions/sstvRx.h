/*
 * Fork: SSTV receive on the 8 kHz FM RX audio.
 *
 * Hardware independent (also compiled on the host for the offline tests).
 * Quadrature FM demodulator (1900 Hz centre), VIS header detection, then the
 * picture is sampled line by line with a mode table, re-timed on every sync
 * pulse. The picture is scaled to SSTV_RX_WIDTH x SSTV_RX_ROWS and delivered
 * pixel by pixel to a callback (on the radio from the I2S DMA interrupt).
 *
 * Modes: Robot 36 / 72, Martin M1 / M2, Scottie S1 / S2, PD 90 / 120 / 180.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_SSTV_RX_H_
#define _OPENGD77_SSTV_RX_H_

#include <stdint.h>
#include <stdbool.h>

#define SSTV_RX_WIDTH           160U
#define SSTV_RX_ROWS            120U
#define SSTV_RX_SCRATCH_SIZE    (4U * SSTV_RX_WIDTH) // 4 channel lines

typedef enum
{
	SSTV_RX_IDLE = 0,    // waiting for a VIS header
	SSTV_RX_RECEIVING,
	SSTV_RX_DONE         // picture complete (or signal lost), waiting for the next VIS
} sstvRxState_t;

typedef void (*sstvRxPixelCallback_t)(uint16_t x, uint16_t row, uint8_t r, uint8_t g, uint8_t b);

typedef struct
{
	sstvRxState_t state;
	const char   *modeName;   // "" when none yet
	uint16_t      row;        // last row written
	uint16_t      freqHz;     // smoothed audio frequency (tuning aid), 0 = no signal
	uint8_t       lastVis;    // last VIS code of an unsupported mode, 0 = none
} sstvRxStatus_t;

// 'scratch' must hold SSTV_RX_SCRATCH_SIZE bytes, it is only used while the decoder runs.
void sstvRxInit(uint8_t *scratch, sstvRxPixelCallback_t pixelCallback);
void sstvRxProcess(const int16_t *samples, uint32_t count, uint32_t stride);
void sstvRxGetStatus(sstvRxStatus_t *status);
void sstvRxRestart(void); // back to waiting for a VIS header

#endif
