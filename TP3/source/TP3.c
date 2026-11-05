/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * TP3: adquisicion y botones existentes; FFT por bloques.
 *
 * SW2:      BYPASS / FFT.
 * SW3:      cambia Fs.
 * Touchpad: selecciona 512, 1024 o 2048 muestras.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_lpuart.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_spc.h"
#include "fsl_tsi_v6.h"
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

#define BUFFER_MAX_SIZE 2048U

#define ADC_MID 2048
#define BYPASS 0
#define MODO_FFT 1
#define NUM_FS_MUESTREO 5

/* Mismos parametros del touchpad del TP2. */
#define TSI_CANAL        3U
#define TOQUE_DELTA      20
#define TOQUE_LECTURAS   3U
#define PERIODO_TSI_MS   10U
#define TIMEOUT_TSI_MS   50U
#define LECTURAS_BASE    16U

volatile int state_counter = 0;
volatile int modo_seleccionado = BYPASS;

uint32_t sample_vel[NUM_FS_MUESTREO] = {
    VEL_0, VEL_1, VEL_2, VEL_3, VEL_4
};

/*
 * BUFFER_SIZE conserva el nombre, pero ahora es una variable.
 * La modifica el touchpad y la consulta la ISR del ADC al
 * comenzar un bloque.
 */
volatile uint16_t BUFFER_SIZE = 512U;

/*
 * Un unico buffer de entrada y uno de salida.
 * Se reserva la capacidad maxima una sola vez.
 */
q15_t buffer_in[BUFFER_MAX_SIZE];

/* Cada resultado complejo ocupa dos posiciones: real e imaginaria. */
float32_t buffer_out[2U * BUFFER_MAX_SIZE];

static const arm_cfft_instance_f32 *pte_fft;

/* Protege el bloque durante FFT y envio. */
volatile bool buffer_listo = false;
volatile bool cambio_fs = true;

/* Configuracion correspondiente al bloque adquirido. */
static int fs_bloque;
static int modo_bloque;
static uint16_t buffer_size_bloque = 512U;

/* Base de tiempo del TP2. */
static volatile uint32_t ms_ticks;

/* Mismo estado del touchpad del TP2; se usa desde main. */
static bool     touch_ok;
static bool     touch_midiendo;
static bool     touch_tocado;
static uint8_t  touch_cuenta;
static int32_t  touch_base;
static uint32_t touch_t0;


/*
 * Conserva el nombre y la firma.
 * La seleccion ahora se hace durante la ejecucion y usa el
 * tamaño del bloque adquirido, no la seleccion pendiente.
 */
void init_fft(void) {
    switch (buffer_size_bloque) {
        case 512U:
            pte_fft = &arm_cfft_sR_f32_len512;
            break;

        case 1024U:
            pte_fft = &arm_cfft_sR_f32_len1024;
            break;

        case 2048U:
            pte_fft = &arm_cfft_sR_f32_len2048;
            break;
    }

    /*
     * La estructura a la que se apunta contiene:
     * - Longitud de la FFT.
     * - Factores TWIDDLE.
     * - Tabla para ordenar los resultados.
     */
}


void setup_new_match(int m_val) {
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

    CTIMER_SetupMatch(
        CTIMER0_PERIPHERAL,
        CTIMER0_MATCH_3_CHANNEL,
        &CTIMER0_Match_3_config
    );

    CTIMER_Reset(CTIMER0_PERIPHERAL);
    CTIMER_StartTimer(CTIMER0_PERIPHERAL);
}


void set_state(int state_number) {
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


/* Reutilizado del TP2: base de tiempo de 1 ms. */
void SysTick_Handler(void) {
    ms_ticks++;
}


/*
 * Reutilizado del TP2.
 * Un barrido con espera activa, solo para medir la base.
 */
static bool tsi_barrido_bloqueante(uint16_t *cuenta) {
    uint32_t t0 = ms_ticks;

    TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);
    TSI_StartSoftwareTrigger(TSI0);

    while (!(TSI_GetStatusFlags(TSI0) & kTSI_EndOfScanFlag)) {
        if (ms_ticks - t0 > TIMEOUT_TSI_MS) {
            return false;
        }
    }

    *cuenta = TSI_GetCounter(TSI0);
    TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);

    return true;
}


/*
 * Reutilizado del TP2.
 *
 * P1_3 como TSI0_CH3 y reloj FRO de 12 MHz configurados
 * mediante BOARD_InitBootPins() y BOARD_InitBootClocks().
 *
 * No tocar el touchpad durante la calibracion al arrancar.
 */
static void init_touch(void) {
    tsi_selfCap_config_t cfg;
    uint32_t suma = 0;
    uint16_t cuenta;

    TSI_GetSelfCapModeDefaultConfig(&cfg);
    TSI_InitSelfCapMode(TSI0, &cfg);
    TSI_EnableNoiseCancellation(TSI0, false);
    TSI_EnableHardwareTriggerScan(TSI0, false);

    TSI_DisableInterrupts(
        TSI0,
        kTSI_GlobalInterruptEnable | kTSI_EndOfScanInterruptEnable
    );

    TSI_SetSelfCapMeasuredChannel(TSI0, TSI_CANAL);
    TSI_EnableModule(TSI0, true);

    for (uint32_t i = 0; i < LECTURAS_BASE; i++) {
        if (!tsi_barrido_bloqueante(&cuenta)) {
            /*
             * touch_ok permanece en false, como en TP2.
             * Se omite PRINTF para usar solamente el protocolo TP3.
             */
            return;
        }

        suma += cuenta;
    }

    touch_base = (int32_t)(suma / LECTURAS_BASE);
    touch_t0 = ms_ticks;
    touch_ok = true;
}


/*
 * Reutilizado del TP2, sin cambiar la deteccion.
 *
 * Devuelve true solo al confirmar un nuevo toque.
 * Mantener el dedo apoyado no genera varios cambios.
 */
static bool touch_nuevo_toque(uint16_t cuenta) {
    int32_t umbral = touch_tocado
        ? touch_base - TOQUE_DELTA / 2
        : touch_base - TOQUE_DELTA;

    bool lectura = (int32_t)cuenta < umbral;

    if (lectura == touch_tocado) {
        touch_cuenta = 0;
        return false;
    }

    if (++touch_cuenta < TOQUE_LECTURAS) {
        return false;
    }

    touch_cuenta = 0;
    touch_tocado = lectura;

    return touch_tocado;
}


/*
 * Reutilizado del TP2.
 * Se conserva el sondeo y se cambia la accion de cada toque.
 */
static void touch_actualizar(uint32_t ahora) {
    if (!touch_ok) {
        return;
    }

    if (!touch_midiendo) {
        if (ahora - touch_t0 >= PERIODO_TSI_MS) {
            TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);
            TSI_StartSoftwareTrigger(TSI0);

            touch_midiendo = true;
            touch_t0 = ahora;
        }

        return;
    }

    if (!(TSI_GetStatusFlags(TSI0) & kTSI_EndOfScanFlag)) {
        if (ahora - touch_t0 > TIMEOUT_TSI_MS) {
            touch_ok = false;
        }

        return;
    }

    uint16_t cuenta = TSI_GetCounter(TSI0);

    TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);
    touch_midiendo = false;

    /*
     * Nueva accion: solicitar el siguiente tamaño.
     * El bloque en curso conserva buffer_size_bloque.
     */
    if (touch_nuevo_toque(cuenta)) {
        switch (BUFFER_SIZE) {
            case 512U:
                BUFFER_SIZE = 1024U;
                break;

            case 1024U:
                BUFFER_SIZE = 2048U;
                break;

            case 2048U:
                BUFFER_SIZE = 512U;
                break;
        }
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

    modo_seleccionado =
        (modo_seleccionado == BYPASS) ? MODO_FFT : BYPASS;

    GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);

#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}


void ADC0_IRQHANDLER(void) {
    uint32_t trigger_status_flag =
        LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL);

    uint32_t status_flag =
        LPADC_GetStatusFlags(ADC0_PERIPHERAL);

    LPADC_ClearTriggerStatusFlags(
        ADC0_PERIPHERAL,
        trigger_status_flag
    );

    LPADC_ClearStatusFlags(ADC0_PERIPHERAL, status_flag);

    static uint16_t index_buffer = 0;
    lpadc_conv_result_t result;

    /*
     * Se conserva el descarte del bloque parcial cuando cambia Fs
     * o se detecta una perdida de muestras.
     *
     * Un bloque completo permanece protegido por buffer_listo.
     */
    if (cambio_fs ||
        (status_flag & (kLPADC_ResultFIFO0OverflowFlag |
                        kLPADC_TriggerExceptionFlag))) {

        index_buffer = 0;
        cambio_fs = false;

        while (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) {
        }

    } else if (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) {
        q15_t muestra =
            (q15_t)((int32_t)result.convValue - 32768);

        if (!buffer_listo) {
            /*
             * Ordena el acceso al buffer despues de observar
             * que main lo dejo disponible.
             */

            if (index_buffer == 0) {
                fs_bloque = state_counter;
                modo_bloque = modo_seleccionado;
                buffer_size_bloque = BUFFER_SIZE;
            }

            buffer_in[index_buffer] = muestra;
            index_buffer++;

            if (index_buffer == buffer_size_bloque) {
                index_buffer = 0;
                buffer_listo = true;
            }
        }
    }

#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}


void procesar_buffer(void) {
    for (uint16_t i = 0; i < buffer_size_bloque; i++) {
        buffer_out[2*i] = (float32_t)buffer_in[i] / 32768.0f;
        buffer_out[2*i+1] = 0.0f;
    }

    if (modo_bloque == MODO_FFT) {
        /*
         * Selecciona la instancia que corresponde a las
         * muestras de ESTE bloque.
         */
        init_fft();

        arm_cfft_f32(pte_fft, buffer_out, 0, 1);

        /*
         * pte_fft: estructura de la FFT seleccionada.
         * buffer_out: datos complejos y resultado.
         * 0: transformada directa.
         * 1: bins en orden natural.
         */
    }
}


/*
 * UART inicializada por BOARD_InitBootPeripherals().
 * Se conserva la funcion existente.
 */
static bool enviar_texto_uart(const char *texto) {
    return LPUART_WriteBlocking(
        LP_FLEXCOMM4_PERIPHERAL,
        (const uint8_t *)texto,
        strlen(texto)
    ) == kStatus_Success;
}


void enviar_buffer(void) {
    static uint32_t numero_bloque = 0;
    char linea[64];

    uint32_t fs_millihz =
        (uint32_t)((1000ULL * CTIMER0_TICK_FREQ) /
        (2ULL *
         (CTIMER0_config.prescale + 1ULL) *
         (sample_vel[fs_bloque] + 1ULL)));

    /*
     * Se conserva el formato que recibe recibir_tp3.m.
     * Fs, modo y tamaño corresponden al bloque adquirido.
     */
    snprintf(
        linea,
        sizeof(linea),
        "TP3,%lu,%u,%u,%lu\r\n",
        (unsigned long)++numero_bloque,
        (unsigned)modo_bloque,
        (unsigned)buffer_size_bloque,
        (unsigned long)fs_millihz
    );

    if (!enviar_texto_uart(linea)) {
        return;
    }

    touch_actualizar(ms_ticks);

    for (uint16_t i = 0; i < buffer_size_bloque; i++) {
        uint32_t real, imag;

        /* Mismo envio hexadecimal de los bits de cada float. */
        memcpy(&real, &buffer_out[2*i], sizeof(real));
        memcpy(&imag, &buffer_out[2*i+1], sizeof(imag));

        snprintf(
            linea,
            sizeof(linea),
            "%u,%08lX,%08lX\r\n",
            (unsigned)i,
            (unsigned long)real,
            (unsigned long)imag
        );

        if (!enviar_texto_uart(linea)) {
            return;
        }

        /*
         * Atender el touchpad entre filas para no esperar
         * hasta el final de toda la trama.
         *
         * Solo cambia BUFFER_SIZE: buffer_size_bloque y
         * los datos enviados permanecen iguales.
         */
        touch_actualizar(ms_ticks);
    }

    (void)enviar_texto_uart("END\r\n");
}


int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();

    SPC_EnableActiveModeAnalogModules(
        SPC0,
        kSPC_controlVref
    );

    BOARD_InitBootPeripherals();

    set_state(state_counter);

    /* Misma base temporal utilizada en TP2. */
    SysTick_Config(SystemCoreClock / 1000U);
    init_touch();

    (void)enviar_texto_uart("TP3 boot\r\n");

    while (1) {
        uint32_t ahora = ms_ticks;
        touch_actualizar(ahora);

        if (buffer_listo) {
            procesar_buffer();
            enviar_buffer();
            buffer_listo = false;
        }
    }

    return 0;
}
