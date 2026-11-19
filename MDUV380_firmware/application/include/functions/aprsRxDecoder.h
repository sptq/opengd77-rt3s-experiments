/*
 * Fork: APRS (AFSK 1200 / AX.25) receive decoder core.
 *
 * Hardware independent: feed it 8 kHz signed 16-bit audio samples, it calls
 * the frame callback for every AX.25 frame whose FCS is valid. The same
 * source is compiled on the host for the offline test harness.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_APRS_RX_DECODER_H_
#define _OPENGD77_APRS_RX_DECODER_H_

#include <stdint.h>
#include <stdbool.h>

#define APRS_RX_SAMPLE_RATE      8000U
#define APRS_RX_BAUD_RATE        1200U
#define APRS_RX_MAX_FRAME_LEN    332U // AX.25: 70 address + 2 ctrl/pid + 256 info + 2 FCS (+ slack)
#define APRS_RX_MIN_FRAME_LEN    18U  // 2 addresses + ctrl + pid + FCS

// Big buffers go to the CCM RAM on the radio (not zeroed at boot: users must initialise them)
#if defined(STM32F405xx)
#define APRS_RX_CCMRAM           __attribute__((section(".ccmram")))
#else
#define APRS_RX_CCMRAM
#endif

// Called for each good frame. 'len' excludes the 2 FCS bytes.
typedef void (*aprsRxFrameCallback_t)(const uint8_t *frame, uint16_t len);

typedef struct
{
	uint32_t framesOk;      // delivered frames (after de-duplication between slicers)
	uint32_t crcErrors;     // frame-sized candidates with a bad FCS (reference slicer only)
} aprsRxDecoderStats_t;

void aprsRxDecoderInit(aprsRxFrameCallback_t callback);
void aprsRxDecoderProcess(const int16_t *samples, uint32_t count, uint32_t stride);
void aprsRxDecoderGetStats(aprsRxDecoderStats_t *stats);
uint8_t *aprsRxDecoderScratch(uint32_t *size); // memory lent to other decoders while APRS is off

#endif
