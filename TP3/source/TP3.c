/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TP3: misma adquisicion y botones del TP2; FIR reemplazado por FFT por bloques.
 * Configuracion actual de pines: SW2 selecciona BYPASS / FFT; SW3 cambia Fs.
 */
#include <stdio.h>
#include <fsl_debug_console.h>
#include <string.h>
#include <stdbool.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_lpuart.h"
#include "fsl_dac.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "arm_math.h"
#include "arm_const_structs.h"

#define VEL_0 9374
#define VEL_1 4687
#define VEL_2 3408
#define VEL_3 1704
#define VEL_4 1562

#define LED_RED_PIN   10U
#define LED_GREEN_PIN 27U
#define LED_BLUE_PIN  2U
#define SW2_PIN       23U
#define SW3_PIN       6U
#ifndef GPIO0_INT_0_IRQHANDLER
#define GPIO0_INT_0_IRQHANDLER GPIO00_IRQHandler
#endif
#ifndef GPIO0_INT_1_IRQHANDLER
#define GPIO0_INT_1_IRQHANDLER GPIO01_IRQHandler
#endif

#define BUFFER_SIZE 512       /* Probar 512, 1024 o 2048, como pide el TP. */
#define ADC_MID 2048
#define BYPASS 0
#define MODO_FFT 1
#define NUM_FS_MUESTREO 5

volatile int state_counter = 0;
volatile int modo_seleccionado = BYPASS;  /* BYPASS o MODO_FFT */
uint32_t sample_vel[NUM_FS_MUESTREO] = {VEL_0, VEL_1, VEL_2, VEL_3, VEL_4};

q15_t buffer_in[BUFFER_SIZE];
/* Cada resultado complejo ocupa dos posiciones: real e imaginaria. */
float32_t buffer_out[2 * BUFFER_SIZE];
static const arm_cfft_instance_f32 *pte_fft;

/* Necesario para no sobrescribir el bloque durante FFT/envio. */
volatile bool buffer_listo = false;
volatile bool cambio_fs = true;
static int fs_bloque; //fs correspondiente a las muestras guardadas
static int modo_bloque; //FFT o bypass

void init_fft(void) {
#if BUFFER_SIZE == 512
    pte_fft = &arm_cfft_sR_f32_len512;
#elif BUFFER_SIZE == 1024
    pte_fft = &arm_cfft_sR_f32_len1024;
#elif BUFFER_SIZE == 2048
    pte_fft = &arm_cfft_sR_f32_len2048;
#else
#error "BUFFER_SIZE debe ser 512, 1024 o 2048"
#endif
//La estructura a la que se apunta contiene:
// - Longitud de la FFT
// - Factores TWIDDLE
// - Tabla para ordenar los resultados
}

//static void init_dac(void) {
//	dac_config_t dacConfig;
//
//	CLOCK_SetClkDiv(kCLOCK_DivDac0Clk, 1u);
//	CLOCK_AttachClk(kFRO_HF_to_DAC0);
//	SPC0->ACTIVE_CFG1 |= 0x11;          /* enciende DAC0 y VREF */
//
//	DAC_GetDefaultConfig(&dacConfig);
//	dacConfig.referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt1;
//	DAC_Init(DAC0, &dacConfig);
//	DAC_Enable(DAC0, true);
//	DAC_SetData(DAC0, ADC_MID);         /* arranca en la mitad de escala */
//}

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

void GPIO0_INT_0_IRQHANDLER(void) {
    uint32_t pin_flags0 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);
    state_counter = (state_counter + 1) % NUM_FS_MUESTREO;
    setup_new_match(sample_vel[state_counter]);
    set_state(state_counter);
    cambio_fs = true;
    GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags0, 0U);
#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}

void GPIO0_INT_1_IRQHANDLER(void) {
    uint32_t pin_flags1 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 1U);
    modo_seleccionado = (modo_seleccionado == BYPASS) ? MODO_FFT : BYPASS;
    GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);
#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}

void ADC0_IRQHANDLER(void) {
    uint32_t trigger_status_flag = LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL);
    uint32_t status_flag = LPADC_GetStatusFlags(ADC0_PERIPHERAL);
    LPADC_ClearTriggerStatusFlags(ADC0_PERIPHERAL, trigger_status_flag);
    LPADC_ClearStatusFlags(ADC0_PERIPHERAL, status_flag);
    static uint16_t index_buffer = 0;
    lpadc_conv_result_t result;

    /* Una FFT requiere muestras consecutivas a una sola Fs. Descartar el
     * bloque parcial y conversiones pendientes tras cambio de Fs o perdida
     * de muestras. Un bloque ya completo queda protegido por buffer_listo.
     */
    if (cambio_fs || (status_flag & (kLPADC_ResultFIFO0OverflowFlag |
                                     kLPADC_TriggerExceptionFlag))) {
        index_buffer = 0;
        cambio_fs = false;
        while (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) { }
    } else if (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) {
        q15_t muestra = (q15_t)((int32_t)result.convValue - 32768);

        if (!buffer_listo) {
            if (index_buffer == 0) {
                fs_bloque = state_counter;
                modo_bloque = modo_seleccionado;
            }
            buffer_in[index_buffer] = muestra;
            index_buffer++;
            if (index_buffer == BUFFER_SIZE) {
                index_buffer = 0;
                //__DMB();  /* Publicar las muestras antes de avisar a main. */
                buffer_listo = true;
            }
        }

//        int32_t out_dac = ((int32_t)muestra / 16) + ADC_MID;
//        if (out_dac < 0) out_dac = 0;
//        if (out_dac > 4095) out_dac = 4095;
//        DAC_SetData(DAC0, (uint16_t)out_dac);
    }
#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}

void procesar_buffer(void) {
    for (uint16_t i = 0; i < BUFFER_SIZE; i++) {
        buffer_out[2*i] = (float32_t)buffer_in[i] / 32768.0f;
        buffer_out[2*i+1] = 0.0f;
    }
    if (modo_bloque == MODO_FFT) {
        arm_cfft_f32(pte_fft, buffer_out, 0, 1);
        // puntero a la struc previamente determinada
        // buffer con los datos a aplicar FFT
        // 0=transformada directa
        // 1=activar reordenamiento para obtener bins en orden natural
    }
}

/* UART inicializada por BOARD_InitBootPeripherals() desde Config Tools.
 * Espera a completar cada envio; las interrupciones del ADC siguen habilitadas.
 */
static bool enviar_texto_uart(const char *texto) {
    return LPUART_WriteBlocking(LP_FLEXCOMM4_PERIPHERAL,
                               (const uint8_t *)texto, strlen(texto)) == kStatus_Success;
}

void enviar_buffer(void) {
    static uint32_t numero_bloque = 0;
    char linea[64];  /* Alcanza para la cabecera o una fila, incluido el terminador. */
    uint32_t fs_millihz = (uint32_t)((1000ULL * CTIMER0_TICK_FREQ) /
        (2ULL * (CTIMER0_config.prescale + 1ULL) * (sample_vel[fs_bloque] + 1ULL)));
    /* Formato que ya recibe recibir_tp3.m. Fs y modo pertenecen a ESTE bloque. */
    snprintf(linea, sizeof(linea), "TP3,%lu,%u,%u,%lu\r\n",
             (unsigned long)++numero_bloque, (unsigned)modo_bloque,
             (unsigned)BUFFER_SIZE, (unsigned long)fs_millihz);
    if (!enviar_texto_uart(linea)) return;
    for (uint16_t i = 0; i < BUFFER_SIZE; i++) {
        uint32_t real, imag;
        /* Enviar los bits de cada float evita habilitar printf de decimales. */
        memcpy(&real, &buffer_out[2*i], sizeof(real));
        memcpy(&imag, &buffer_out[2*i+1], sizeof(imag));
        snprintf(linea, sizeof(linea), "%u,%08lX,%08lX\r\n", (unsigned)i,
                 (unsigned long)real, (unsigned long)imag);
        PRINTF("%d", buffer_out[2*i]);
        if (!enviar_texto_uart(linea)) return;
    }
    (void)enviar_texto_uart("END\r\n");
}

int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    //init_dac();
    BOARD_InitBootPeripherals();
    init_fft();
    set_state(state_counter);

    while (1) {
        if (buffer_listo) {
            //__DMB();
            procesar_buffer();
            enviar_buffer();
            //__DMB();
            buffer_listo = false; 
        }
    }
    return 0;
}
