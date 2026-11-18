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
#include "main.h"
#include "interfaces/i2s.h"

volatile bool g_TX_SAI_in_use = false;
volatile bool isSending = false;
volatile bool isReceiving = false;

volatile bool stopOnNextI2SDMAInterrupt = false;

uint16_t i2s_Tx_Buffer[NUM_I2S_BUFFERS][2][WAV_BUFFER_SIZE];
uint16_t i2s_Rx_Buffer[NUM_I2S_BUFFERS][2][WAV_BUFFER_SIZE];

#if defined(I2S_RX_AUDIO_PROBE)
// Fork: diagnostic "RX audio probe". Captures a short burst of whatever the
// HR-C6000 sends on I2S while nothing else uses it (no TX, no beep/voice
// prompt), to find out whether FM RX audio reaches the MCU (prerequisite for
// APRS RX). The burst stops itself from the DMA callback.
#define I2S_PROBE_BURST_HALVES   25U   // 25 x 20 ms = 0.5 s
#define I2S_PROBE_ZC_HYSTERESIS  256
static volatile bool i2sProbeActive = false;
static volatile uint16_t i2sProbeHalves;
static volatile uint16_t i2sProbePeak;
static volatile uint32_t i2sProbeZc;
static int8_t i2sProbeSign;
static volatile uint16_t i2sProbeLastPeak;
static volatile uint32_t i2sProbeLastZc;
static volatile uint32_t i2sProbeBursts;
static void i2sProbeProcess(uint32_t bufNum);
#endif

static void clearI2SBuffersAndFlags(void)
{
	memset(i2s_Tx_Buffer, 0x00, (NUM_I2S_BUFFERS * 2 * (WAV_BUFFER_SIZE * sizeof(uint16_t))));
	memset(i2s_Rx_Buffer, 0x00, (NUM_I2S_BUFFERS * 2 * (WAV_BUFFER_SIZE * sizeof(uint16_t))));
	stopOnNextI2SDMAInterrupt = false;
	isSending = false;
	isReceiving = false;
	 HAL_I2S_DMAStop(&hi2s3);
	 __HAL_I2SEXT_FLUSH_RX_DR(&hi2s3);
	g_TX_SAI_in_use = false;
#if defined(I2S_RX_AUDIO_PROBE)
	i2sProbeActive = false; // a beep took over the DMA during a probe burst
#endif
	soundInit();
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (!stopOnNextI2SDMAInterrupt)
	{
		if (isSending)
		{
			g_TX_SAI_in_use = soundRefillData(0);
		}
		else if(isReceiving)
		{
			soundReceiveRefillData(0);
		}
#if defined(I2S_RX_AUDIO_PROBE)
		else if (i2sProbeActive)
		{
			i2sProbeProcess(0);
		}
#endif
	}
	else
	{
		clearI2SBuffersAndFlags();
	}
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (!stopOnNextI2SDMAInterrupt)
	{
		if (isSending)
		{
			g_TX_SAI_in_use = soundRefillData(1);
		}
		else if(isReceiving)
		{
			soundReceiveRefillData(1);
		}
#if defined(I2S_RX_AUDIO_PROBE)
		else if (i2sProbeActive)
		{
			i2sProbeProcess(1);
		}
#endif
	}
	else
	{
		clearI2SBuffersAndFlags();
	}
}

void I2SStartDMA(uint16_t *txbuff, uint16_t *rxbuff, size_t bufferLen)
{
	// Need to wait for WS line has just toggled from logic low to high
	while(!HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	while(!HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	stopOnNextI2SDMAInterrupt = false;
	HAL_I2SEx_TransmitReceive_DMA(&hi2s3, txbuff, rxbuff, bufferLen);
}

void I2SReset(void)
{

}

void I2STerminateTransfers(void)
{
	stopOnNextI2SDMAInterrupt = true;
}

void init_I2S(void)
{

}

#if defined(I2S_RX_AUDIO_PROBE)
static void i2sProbeProcess(uint32_t bufNum)
{
	for (int j = 0; j < 2; j++)
	{
		for (int i = 0; i < (WAV_BUFFER_SIZE / 2); i++)
		{
			int16_t v = (int16_t)i2s_Rx_Buffer[bufNum][j][i * 2]; // Left channel only
			uint16_t a = (uint16_t)((v < 0) ? -(int32_t)v : v);

			if (a > i2sProbePeak)
			{
				i2sProbePeak = a;
			}

			// Zero crossings with hysteresis, so noise around 0 is not counted
			if ((v > I2S_PROBE_ZC_HYSTERESIS) && (i2sProbeSign <= 0))
			{
				if (i2sProbeSign < 0)
				{
					i2sProbeZc++;
				}
				i2sProbeSign = 1;
			}
			else if ((v < -I2S_PROBE_ZC_HYSTERESIS) && (i2sProbeSign >= 0))
			{
				if (i2sProbeSign > 0)
				{
					i2sProbeZc++;
				}
				i2sProbeSign = -1;
			}
		}
	}

	if (++i2sProbeHalves >= I2S_PROBE_BURST_HALVES)
	{
		HAL_I2S_DMAStop(&hi2s3);
		__HAL_I2SEXT_FLUSH_RX_DR(&hi2s3);
		i2sProbeLastPeak = i2sProbePeak;
		i2sProbeLastZc = i2sProbeZc;
		i2sProbeBursts++;
		i2sProbeActive = false;
	}
}

// Wait for a WS rising edge, with a bounded number of polls (WS is driven by
// the HR-C6000; when it does not clock I2S, I2SStartDMA() would hang).
static bool i2sProbeWaitWS(GPIO_PinState level)
{
	for (uint32_t n = 0; n < 200000U; n++)
	{
		if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == level)
		{
			return true;
		}
	}
	return false;
}

i2sProbeStatus_t i2sProbeStart(void)
{
	if (i2sProbeActive)
	{
		if ((HAL_I2S_GetState(&hi2s3) == HAL_I2S_STATE_READY) && (isSending == false) && (isReceiving == false))
		{
			i2sProbeActive = false; // the burst was stopped by someone else, recover
		}
		else
		{
			return I2S_PROBE_RUNNING;
		}
	}

	if (g_TX_SAI_in_use || isSending || isReceiving || (HAL_I2S_GetState(&hi2s3) != HAL_I2S_STATE_READY))
	{
		return I2S_PROBE_BUSY;
	}

	if ((i2sProbeWaitWS(GPIO_PIN_SET) && i2sProbeWaitWS(GPIO_PIN_RESET) && i2sProbeWaitWS(GPIO_PIN_SET)) == false)
	{
		return I2S_PROBE_NO_CLOCK;
	}

	memset(i2s_Tx_Buffer, 0x00, sizeof(i2s_Tx_Buffer)); // send silence
	i2sProbeHalves = 0U;
	i2sProbePeak = 0U;
	i2sProbeZc = 0U;
	i2sProbeSign = 0;
	stopOnNextI2SDMAInterrupt = false;
	i2sProbeActive = true;

	if (HAL_I2SEx_TransmitReceive_DMA(&hi2s3, (uint16_t *)i2s_Tx_Buffer, (uint16_t *)i2s_Rx_Buffer, (NUM_I2S_BUFFERS * 2 * WAV_BUFFER_SIZE)) != HAL_OK)
	{
		i2sProbeActive = false;
		return I2S_PROBE_BUSY;
	}

	return I2S_PROBE_STARTED;
}

void i2sProbeGetLast(uint16_t *peak, uint32_t *zeroCrossingsPerSecond, uint32_t *bursts)
{
	*peak = i2sProbeLastPeak;
	*zeroCrossingsPerSecond = (i2sProbeLastZc * 1000U) / (I2S_PROBE_BURST_HALVES * 20U);
	*bursts = i2sProbeBursts;
}
#endif
