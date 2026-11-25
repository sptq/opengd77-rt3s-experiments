/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _OPENGD77_ADC_H_
#define _OPENGD77_ADC_H_

#include <FreeRTOS.h>
#include <task.h>

#if defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
#define NO_ADC_CHANNEL_OVERRIDE    0

#endif // PLATFORM_MD9600
extern const int CUTOFF_VOLTAGE_UPPER_HYST;
extern const int CUTOFF_VOLTAGE_LOWER_HYST;
extern const int BATTERY_MAX_VOLTAGE;
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
extern const int POWEROFF_VOLTAGE_THRESHOLD;
#endif // PLATFORM_MD9600
extern const int TEMPERATURE_DECIMAL_RESOLUTION;

#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
#define NO_ADC_CHANNEL_OVERRIDE                0
#define NUM_ADC_CHANNELS	                   4
#define BATTERY_VOLTAGE_STABILISATION_TIME  1500U // time in PIT ticks for the battery voltage from the ADC to stabilize
#else // PLATFORM_MD9600
#define NUM_ADC_CHANNELS	9
#define NUM_ADC_SAMPLES  8           //keep multiple successive samples for averaging out noise.
#endif // PLATFORM_MD9600

#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
extern volatile uint16_t adcVal[NUM_ADC_CHANNELS];
#else // PLATFORM_MD9600
extern volatile uint8_t adcIndex;
extern volatile uint16_t adcVal[NUM_ADC_CHANNELS][NUM_ADC_SAMPLES];
#endif // PLATFORM_MD9600

void adcTriggerConversion(int channelOverride);
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
void adcStartDMA(void);
#endif // PLATFORM_MD9600
int adcGetBatteryVoltage(void);
int adcGetVOX(void);
int getTemperature(void);
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
int8_t getVolumeControl(void);
#else // PLATFORM_MD9600
int adcGetMicRow(void);
int adcGetMicCol(void);
int adcGetVHFRSSI(void);
int adcGetUHFRSSI(void);
int adcGetVHFNoise(void);
int adcGetUHFNoise(void);
#endif // PLATFORM_MD9600


#endif /* _OPENGD77_ADC_H_ */
