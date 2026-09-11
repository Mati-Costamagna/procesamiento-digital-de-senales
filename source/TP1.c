/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    TP1.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "arm_math.h"
#include "fsl_ctimer.h"
#include "fsl_lpadc.h"
/* TODO: insert other include files here. */

/* TODO: insert other definitions and declarations here. */
#define MAX_FREQ 1e6

#define VEL_0 	8e3
#define VEL_1 	16e3
#define VEL_2	22e3
#define VEL_3	44e3
#define VEL_4	48e3

#define LED_RED_PIN BOARD_INITLEDSPINS_LED_RED_PIN
#define LED_GREEN_PIN BOARD_INITLEDSPINS_LED_GREEN_PIN
#define LED_BLUE_PIN BOARD_INITLEDSPINS_LED_BLUE_PIN

#define SAMPLES_NUM 512

int state_counter = 0;
int flag_adc = 0;

uint32_t sample_vel[5] = {VEL_0, VEL_1, VEL_2, VEL_3, VEL_4};
q15_t samples[SAMPLES_NUM];


void setup_new_match(int freq){

	uint32_t match_val = (MAX_FREQ / freq) - 1;
	CTIMER_StopTimer(CTIMER0_PERIPHERAL);
	const ctimer_match_config_t CTIMER0_Match_3_config = {
	  .matchValue = match_val,
	  .enableCounterReset = true,
	  .enableCounterStop = false,
	  .outControl = kCTIMER_Output_NoAction,
	  .outPinInitState = false,
	  .enableInterrupt = true
	};
	CTIMER_SetupMatch(CTIMER0_PERIPHERAL, CTIMER0_MATCH_3_CHANNEL, &CTIMER0_Match_3_config);
	CTIMER_StartTimer(CTIMER0_PERIPHERAL);
}


void set_state(int state_number){
	switch (state_number) {
			case 0:
				GPIO_PinWrite(GPIO0, LED_RED_PIN, 1);
				GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
				GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
				break;
			case 1:
				GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
				GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
				GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
				break;
			case 2:
				GPIO_PinWrite(GPIO0, LED_RED_PIN, 1);
				GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 1);
				GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 0);
				break;
			case 3:
				GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
				GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 1);
				GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
				break;
			default:
				GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
				GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
				GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 0);
				break;
		}
}

/* GPIO00_IRQn interrupt handler */
void GPIO0_INT_0_IRQHANDLER(void) {
  /* Get pin flags 0 */
  uint32_t pin_flags0 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);

  /* Place your interrupt code here */
  state_counter = (state_counter + 1) % (sizeof(sample_vel) / sizeof(sample_vel[0]));
  setup_new_match(sample_vel[state_counter]);
  set_state(state_counter);  /* Clear pin flags 0 */
  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags0, 0U);

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* GPIO01_IRQn interrupt handler */
void GPIO0_INT_1_IRQHANDLER(void) {
  /* Get pin flags 1 */
  uint32_t pin_flags1 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 1U);

  /* Place your interrupt code here */
  flag_adc = ~flag_adc & 1;
  /* Clear pin flags 1 */
  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* ADC0_IRQn interrupt handler */
void ADC0_IRQHANDLER(void) {
  uint32_t trigger_status_flag;
  uint32_t status_flag;
  /* Trigger interrupt flags */
  trigger_status_flag = LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL);
  /* Interrupt flags */
  status_flag = LPADC_GetStatusFlags(ADC0_PERIPHERAL);
  /* Clears trigger interrupt flags */
  LPADC_ClearTriggerStatusFlags(ADC0_PERIPHERAL, trigger_status_flag);
  /* Clears interrupt flags */
  LPADC_ClearStatusFlags(ADC0_PERIPHERAL, status_flag);

  /* Place your code here */
  static uint16_t sample_count = 0;
  lpadc_conv_result_t result;
  LPADC_GetConvResultBlocking(ADC0_PERIPHERAL, &result, 0);
  if (flag_adc){
	  samples[sample_count] = (q15_t)result.convValue;
	  sample_count = (sample_count + 1) % SAMPLES_NUM;
  }
  PRINTF("%d \n", state_counter);
  PRINTF("%d \n", samples[sample_count]);

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}


/*
 * @brief   Application entry point.
 */
int main(void) {

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    BOARD_InitLEDsPins();
    BOARD_InitBUTTONsPins();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

    setup_new_match(sample_vel[0]);
    PRINTF("Hello World\r\n");


    /* Enter an infinite loop, just incrementing a counter. */
    while(1) {
//    	PRINTF("%d", state_counter);
    }
    return 0 ;
}
