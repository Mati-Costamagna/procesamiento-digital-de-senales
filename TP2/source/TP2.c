/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    TP2.c
 * @brief   TP2 - Filtros FIR por muestras (CMSIS-DSP, Q15) en FRDM-MCXN947.
 *
 * Boton GPIO0 INT0: cambia la frecuencia de muestreo (8k, 16k, 22k, 44k, 48k).
 *                   El color del LED indica la fs (igual que en el Lab #1).
 * Boton GPIO0 INT1: recorre BYPASS -> PB -> PA -> PBanda -> EB -> BYPASS ...
 *                   (habilita el filtro o hace bypass al buffer de salida).
 */
#include <filtros.h>      /* generado con generar_filtros.m */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_dac.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "arm_math.h"

#define VEL_0 	9374         /*  8 kHz */
#define VEL_1 	4687         /* 16 kHz */
#define VEL_2	3408         /* 22 kHz */
#define VEL_3	1704         /* 44 kHz */
#define VEL_4	1562         /* 48 kHz */

/* Pines de la FRDM-MCXN947 (board.h del SDK). LEDs activos en bajo. */
#define LED_RED_PIN   10U    /* P0_10 -> GPIO0 */
#define LED_GREEN_PIN 27U    /* P0_27 -> GPIO0 */
#define LED_BLUE_PIN  2U     /* P1_2  -> GPIO1 */
#define SW2_PIN       23U    /* P0_23: cambia la fs        -> GPIO0 INT0 */
#define SW3_PIN       6U     /* P0_6 : bypass / tipo filtro -> GPIO0 INT1 */

/* Si las Config Tools no generaron los nombres de los handlers, se usan
 * directamente los del vector de interrupciones. */
#ifndef GPIO0_INT_0_IRQHANDLER
#define GPIO0_INT_0_IRQHANDLER GPIO00_IRQHandler
#endif
#ifndef GPIO0_INT_1_IRQHANDLER
#define GPIO0_INT_1_IRQHANDLER GPIO01_IRQHandler
#endif

#define BUFFER_SIZE 512
#define BLOCK_SIZE  1        /* procesamiento por muestras */

#define ADC_MID   2048       /* mitad de escala del DAC (12 bits) */

#define BYPASS 0             /* filter_counter: 0 = bypass, 1..4 = PB, PA, PBanda, EB */

/* Cantidad de frecuencias de muestreo del ADC (8k, 16k, 22k, 44k, 48k),
 * independiente de cuantas filas de filtros traiga FiltrosTP2.h */
#define NUM_FS_MUESTREO 5

/* Experimento: 1 = usar siempre los coeficientes de 8 kHz con todas las fs.
 * Si FiltrosTP2.h se genero solo para 8 kHz (NUM_FS == 1) se hace igual. */
#define SOLO_8K 0
#define FILA_FILTRO(fs) ((SOLO_8K || NUM_FS == 1) ? K8 : (fs))

volatile int state_counter  = 0;   /* indice de fs de muestreo (0..4) */
volatile int filter_counter = BYPASS;

uint32_t sample_vel[NUM_FS_MUESTREO] = {VEL_0, VEL_1, VEL_2, VEL_3, VEL_4};

/* Buffers de entrada (muestras del ADC) y de salida (van al DAC) */
q15_t buffer_in[BUFFER_SIZE];
q15_t buffer_out[BUFFER_SIZE];

/* Una instancia por filtro [fs][tipo]. Como hay un solo filtro activo a la
 * vez, todas comparten el mismo buffer de estado (se borra al cambiar). */
static arm_fir_instance_q15 arreglo_filtros[NUM_FS][NUM_TIPOS];
static q15_t estado_fir[MAX_TAPS + BLOCK_SIZE];
static arm_fir_instance_q15 *volatile pte_aux = NULL;

volatile bool     cambio_filtro = true;  /* lo pone un boton, lo aplica el ADC */

/******************************************************************************/

void init_filtros(void) {
	for (int i = 0; i < NUM_FS; i++) {
		for (int k = 0; k < NUM_TIPOS; k++) {
			arm_status status = arm_fir_init_q15(&arreglo_filtros[i][k],
					taps_tabla[i][k], coef_tabla[i][k], estado_fir, BLOCK_SIZE);
			if (status != ARM_MATH_SUCCESS) {
				PRINTF("Error inicializando %s a %u Hz\r\n", nombre_tipo[k], fs_hz[i]);
			}
		}
	}
	pte_aux = &arreglo_filtros[FILA_FILTRO(state_counter)][0];
}

/* Reloj, alimentacion e inicializacion del DAC0 (misma secuencia que el
 * ejemplo dac_basic del SDK para FRDM-MCXN947). Tiene que correr ANTES de
 * BOARD_InitBootPeripherals(), porque desde ahi el ADC ya empieza a
 * interrumpir y la ISR escribe en el DAC. */
static void init_dac(void) {
	dac_config_t dacConfig;

	CLOCK_SetClkDiv(kCLOCK_DivDac0Clk, 1u);
	CLOCK_AttachClk(kFRO_HF_to_DAC0);
	SPC0->ACTIVE_CFG1 |= 0x11;          /* enciende DAC0 y VREF */

	DAC_GetDefaultConfig(&dacConfig);
	dacConfig.referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt1;
	DAC_Init(DAC0, &dacConfig);
	DAC_Enable(DAC0, true);
	DAC_SetData(DAC0, ADC_MID);         /* arranca en la mitad de escala */
}

/* Se llama desde la ISR del ADC, entre dos muestras, asi el cambio de
 * filtro nunca ocurre en medio de un arm_fir_q15(). */
static void aplicar_seleccion(void) {
	int fs = state_counter;
//	int fs = 0;
	int f  = filter_counter;
	cambio_filtro = false;
	if (f != BYPASS && pte_aux != NULL) {
		pte_aux = &arreglo_filtros[FILA_FILTRO(fs)][f - 1];
		memset(estado_fir, 0, sizeof(estado_fir));   /* sin historia del filtro anterior */
	}
}

void setup_new_match(int m_val){
	uint32_t match_val = m_val;
	CTIMER_StopTimer(CTIMER0_PERIPHERAL);
	const ctimer_match_config_t CTIMER0_Match_3_config = {
	  .matchValue = match_val,
	  .enableCounterReset = true,
	  .enableCounterStop = false,
	  .outControl = kCTIMER_Output_Toggle,
	  .outPinInitState = false,
	  .enableInterrupt = false
	};
	CTIMER_SetupMatch(CTIMER0_PERIPHERAL, CTIMER0_MATCH_3_CHANNEL, &CTIMER0_Match_3_config);
	CTIMER_Reset(CTIMER0_PERIPHERAL);
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

/* GPIO00_IRQn interrupt handler: cambia la frecuencia de muestreo */
void GPIO0_INT_0_IRQHANDLER(void) {
  uint32_t pin_flags0 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);

  state_counter = (state_counter + 1) % NUM_FS_MUESTREO;
  setup_new_match(sample_vel[state_counter]);
  set_state(state_counter);
  cambio_filtro = true;          /* hay que usar los coeficientes de la nueva fs */

  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags0, 0U);
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* GPIO01_IRQn interrupt handler: bypass / tipo de filtro */
void GPIO0_INT_1_IRQHANDLER(void) {
  uint32_t pin_flags1 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 1U);

  filter_counter = (filter_counter + 1) % (NUM_TIPOS + 1);
  cambio_filtro = true;

  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* ADC0_IRQn interrupt handler: una muestra por interrupcion */
void ADC0_IRQHANDLER(void) {
  uint32_t trigger_status_flag;
  uint32_t status_flag;
  trigger_status_flag = LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL);
  status_flag = LPADC_GetStatusFlags(ADC0_PERIPHERAL);
  LPADC_ClearTriggerStatusFlags(ADC0_PERIPHERAL, trigger_status_flag);
  LPADC_ClearStatusFlags(ADC0_PERIPHERAL, status_flag);

  static uint16_t index_buffer = 0;
  lpadc_conv_result_t result;

  if (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) {
    if (cambio_filtro) {
      aplicar_seleccion();
    }

    /* ADC de 16 bits (kLPADC_ConversionResolutionHigh): 0..65535 -> Q15 centrado en 0 */
    buffer_in[index_buffer] = (q15_t)((int32_t)result.convValue - 32768);

    if (filter_counter == BYPASS || pte_aux == NULL) {
      buffer_out[index_buffer] = buffer_in[index_buffer];      /* bypass */
    } else {
      arm_fir_q15(pte_aux, &buffer_in[index_buffer], &buffer_out[index_buffer], BLOCK_SIZE);
    }

    /* Q15 -> 12 bits para el DAC */
    int32_t out_dac = ((int32_t)buffer_out[index_buffer] / 16) + ADC_MID;
    if (out_dac < 0)    out_dac = 0;
    if (out_dac > 4095) out_dac = 4095;
    DAC_SetData(DAC0, (uint16_t)out_dac);

    index_buffer = (index_buffer + 1) % BUFFER_SIZE;
  }

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
    init_dac();                     /* antes de que arranque el ADC */
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

    init_filtros();
    set_state(state_counter);
    cambio_filtro = true;

    PRINTF("TP2 - FIR por muestras\r\n");

    while (1) {
        __asm volatile ("nop");
    }
    return 0 ;
}
