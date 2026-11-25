/*
 * Copyright (C) 2021-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
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

#ifndef _BATTERYANDPOWERMANAGEMENT_H_
#define _BATTERYANDPOWERMANAGEMENT_H_

#include "functions/ticks.h"
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
#include "user_interface/menuSystem.h"
#endif // PLATFORM_MD9600

#if defined(PLATFORM_MD9600)
#define RADIO_POWER_STATE_RUNNING_FLAG  0xCAFE
#define RADIO_POWER_STATE_STANDBY_FLAG  0x2BED
#endif

extern volatile int batteryVoltage;
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
extern volatile uint16_t micLevel;
extern volatile uint16_t potLevel;
extern volatile uint16_t temperatureLevel;
extern volatile int lastValidBatteryVoltage;
#endif // PLATFORM_MD9600
extern volatile float averageBatteryVoltage;
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
extern volatile float averageBatteryVoltageFine; // Fork: not truncated to 0.1 V (runtime estimate)
extern volatile uint32_t resumeTicks;
#endif // PLATFORM_MD9600

#if !defined(PLATFORM_GD77S)
extern ticksTimer_t apoTimer;
#endif

#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600

#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
bool powerRotarySwitchIsOn(void);
#endif
#endif // PLATFORM_MD9600
void showLowBattery(void);
bool batteryIsLowWarning(void);
bool batteryIsLowVoltageWarning(void);
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
bool batteryIsLowCriticalVoltage(void);
bool batteryLastReadingIsCritical(void);
void batteryChecking(uiEvent_t *ev);
#else // PLATFORM_MD9600
bool batteryIsLowCriticalVoltage(bool isSuspended);
bool batteryLastReadingIsCritical(bool isSuspended);
void batteryChecking(void);
#endif // PLATFORM_MD9600
void batteryUpdate(void);
#if ! defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
int batteryTimeLeftMinutes(void);                  // Fork: BATT_RT_UNKNOWN (-1) or minutes
void batteryTimeLeftString(char *buf, size_t size); // Fork: "~3h20", "~45m", "--"
#endif // PLATFORM_MD9600
void die(bool usbMonitoring, bool maintainRTC, bool forceSuspend, bool safeBoot);
void wakeFromSleep(void);
void powerOffFinalStage(bool maintainRTC, bool forceSuspend);
#if defined(PLATFORM_MD9600) // upstream PLATFORM_MD9600
void powerOff();
#endif // PLATFORM_MD9600
void powerDown(bool doNotSavePowerOffState);
void apoTick(bool eventFromOperator);

#endif
