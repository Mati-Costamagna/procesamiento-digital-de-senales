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
 * Boton GPIO0 INT1: recorre PB -> PA -> PBanda -> EB -> PB ...
 * Pin   GPIO1 INT0: (P1_23, J1-8) alterna lo que va al DAC: buffer de salida
 *                   (filtrado) o muestra tomada del ADC (sin filtrar).
 *                   El filtro solo se calcula con la salida filtrada.
 * P0_26 (J2[10]):   pin de medicion. Esta en 1 mientras se ejecuta la rutina
 *                   del ADC; con el osciloscopio se mide la duracion del pulso.
 *
 * Filtros multitasa (filtros_multitasa.h, generado con disenar_multitasa.m):
 *   PB     : FIR directo a fs.
 *   PA, EB : y[n] = x[n - D] - v[n], donde v es el camino angosto
 *            HD -> diezmado por M -> G a 1 kHz -> interpolador (M * HD).
 *   PBanda : el PA seguido del pasa bajos LPB a fs.
 */
#include <filtros_multitasa.h>   /* generado con disenar_multitasa.m */
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
#define SW2_PIN       23U    /* P0_23: tipo de filtro      -> GPIO0 INT1 */
#define SW3_PIN       6U     /* P0_6 : cambia la fs        -> GPIO0 INT0 */
#define SEL_PIN       23U    /* P1_23: filtrado / sin filtrar -> GPIO1 INT0 */

/* Pin de medicion del tiempo de la ISR del ADC (libre en la placa) */
#define MEDIR_TIEMPO_ISR 1     /* 0 = desactivado */
#define PIN_MEDICION     26U   /* P0_26 -> J2[10] */

/* Si las Config Tools no generaron los nombres de los handlers, se usan
 * directamente los del vector de interrupciones. */
#ifndef GPIO0_INT_0_IRQHANDLER
#define GPIO0_INT_0_IRQHANDLER GPIO00_IRQHandler
#endif
#ifndef GPIO0_INT_1_IRQHANDLER
#define GPIO0_INT_1_IRQHANDLER GPIO01_IRQHandler
#endif
#ifndef GPIO1_INT_0_IRQHANDLER
#define GPIO1_INT_0_IRQHANDLER GPIO10_IRQHandler
#endif

#define BUFFER_SIZE 512
#define BLOCK_SIZE  1        /* procesamiento por muestras */

#define ADC_MID   2048       /* mitad de escala del DAC (12 bits) */

/* Cantidad de frecuencias de muestreo del ADC (8k, 16k, 22k, 44k, 48k),
 * independiente de cuantas filas de filtros traiga filtros_multitasa.h */
#define NUM_FS_MUESTREO 5

/* Experimento: 1 = usar siempre los coeficientes de 8 kHz con todas las fs.
 * Si el header se genero solo para 8 kHz (NUM_FS == 1) se hace igual. */
#define SOLO_8K 0
#define FILA_FILTRO(fs) ((SOLO_8K || NUM_FS == 1) ? K8 : (fs))

#define MAX_TAPS_G ((NUM_TAPS_G_PA > NUM_TAPS_G_EB) ? NUM_TAPS_G_PA : NUM_TAPS_G_EB)

volatile int state_counter  = 0;   /* indice de fs de muestreo (0..4) */
volatile int filter_counter = PB;  /* tipo de filtro: PB, PA, PBANDA, EB */
volatile bool salida_filtrada = true; /* false: al DAC va la muestra del ADC */

uint32_t sample_vel[NUM_FS_MUESTREO] = {VEL_0, VEL_1, VEL_2, VEL_3, VEL_4};

/* Buffers de entrada (muestras del ADC) y de salida (van al DAC) */
q15_t buffer_in[BUFFER_SIZE];
q15_t buffer_out[BUFFER_SIZE];

/* Instancias de CMSIS-DSP: PB y LPB a cada fs, G_PA y G_EB a F_BAJA_HZ.
 * Como hay un solo filtro activo a la vez, los de cada grupo comparten el
 * buffer de estado (se borra al cambiar). */
static arm_fir_instance_q15 fir_pb[NUM_FS];
static arm_fir_instance_q15 fir_lpb[NUM_FS];
static arm_fir_instance_q15 fir_g[2];          /* 0: G_PA, 1: G_EB */
static q15_t estado_fs[MAX_TAPS_FS + BLOCK_SIZE];
static q15_t estado_g[MAX_TAPS_G + BLOCK_SIZE];

/* Filtro activo (lo fija aplicar_seleccion) */
static int tipo_activo = PB;
static arm_fir_instance_q15 *fir_fs_activo;    /* PB o LPB de la fs activa */

/* Camino angosto: HD -> diezmado por M -> G -> interpolador polifasico.
 * Es la traduccion de cadena_muestra_a_muestra() de disenar_multitasa.m. */
typedef struct {
	const q15_t *hd;              /* HD rellenado con ceros hasta L * M */
	uint16_t m;                   /* factor de diezmado, fs / F_BAJA_HZ */
	uint16_t l;                   /* coeficientes de HD por fase, HD_PAD / M */
	uint16_t d;                   /* retardo del complemento x[n - D] */
	arm_fir_instance_q15 *g;      /* G_PA o G_EB */
} camino_angosto_t;

static camino_angosto_t camino;
static q15_t    hist_x[LONG_HIST_X];  /* historia de x, para x[n - D] */
static uint16_t pos_x;                /* posicion de x[n] en hist_x */
static int64_t  acc_hd[MAX_HIST_W];   /* diezmados pendientes (HD repartido) */
static q15_t    hist_w[MAX_HIST_W];   /* salidas de G, hist_w[0] la mas nueva */
static uint16_t fase;                 /* n % M */
static uint16_t ranura;               /* (n / M) % L: acumulador de u[n / M] */

volatile bool cambio_filtro  = true;   /* lo pone un boton, lo aplica el ADC */
volatile bool filtros_listos = false;  /* hasta init_filtros() la salida es x */

/******************************************************************************/

static void init_fir(arm_fir_instance_q15 *s, uint16_t taps, const q15_t *coef,
		q15_t *estado, const char *nombre, uint32_t fs) {
	if (arm_fir_init_q15(s, taps, coef, estado, BLOCK_SIZE) != ARM_MATH_SUCCESS) {
		PRINTF("Error inicializando %s a %u Hz\r\n", nombre, fs);
	}
}

void init_filtros(void) {
	for (int i = 0; i < NUM_FS; i++) {
		init_fir(&fir_pb[i],  pb_taps[i],  pb_tabla[i],  estado_fs, "PB",  fs_hz[i]);
		init_fir(&fir_lpb[i], lpb_taps[i], lpb_tabla[i], estado_fs, "LPB", fs_hz[i]);
	}
	init_fir(&fir_g[0], NUM_TAPS_G_PA, G_PA, estado_g, "G_PA", F_BAJA_HZ);
	init_fir(&fir_g[1], NUM_TAPS_G_EB, G_EB, estado_g, "G_EB", F_BAJA_HZ);
	filtros_listos = true;
}

/* Una muestra del camino angosto. Devuelve x[n - D] - v[n] (PA o EB).
 * HD repartido: en lugar de calcular u[k] = sum HD[j] x[kM - j] de golpe en
 * el instante de diezmado, cada x[n] suma sus L productos a los L diezmados
 * que todavia esperan esa muestra. Da el mismo u[k], sin el pico de calculo. */
static q15_t complemento_multitasa(q15_t x) {
	const camino_angosto_t *c = &camino;
	const uint16_t M = c->m;
	const uint16_t L = c->l;

	pos_x = (pos_x + 1 == LONG_HIST_X) ? 0 : pos_x + 1;
	hist_x[pos_x] = x;

	/* Con fase p = n % M, x[n] = x[kM - j] aporta a u[n/M] con j = 0 si
	 * p == 0, y a u[n/M + 1], u[n/M + 2]... con j = M - p, 2M - p... */
	uint16_t j = (fase == 0) ? 0 : M - fase;
	uint16_t r = (fase == 0) ? ranura : ((ranura + 1 == L) ? 0 : ranura + 1);
	for (uint16_t i = 0; i < L; i++) {
		acc_hd[r] += (int32_t)c->hd[j] * x;
		j += M;
		r = (r + 1 == L) ? 0 : r + 1;
	}

	/* Instante de diezmado: u[n/M] esta completo y pasa por G a F_BAJA_HZ */
	if (fase == 0) {
		q15_t u = (q15_t)__SSAT((int32_t)(acc_hd[ranura] >> 15), 16);
		q15_t w;
		acc_hd[ranura] = 0;
		arm_fir_q15(c->g, &u, &w, BLOCK_SIZE);
		for (uint16_t m = L - 1; m > 0; m--) {
			hist_w[m] = hist_w[m - 1];
		}
		hist_w[0] = w;
	}

	/* Interpolador polifasico: v[n] = M * sum_m HD[p + mM] w[n/M - m] */
	int64_t acc = 0;
	const q15_t *h = &c->hd[fase];
	for (uint16_t m = 0; m < L; m++) {
		acc += (int32_t)h[m * M] * hist_w[m];
	}
	int32_t v = __SSAT((int32_t)((acc * M) >> 15), 16);

	if (++fase == M) {
		fase = 0;
		ranura = (ranura + 1 == L) ? 0 : ranura + 1;
	}

	int32_t k = (int32_t)pos_x - c->d;
	if (k < 0) {
		k += LONG_HIST_X;
	}
	return (q15_t)__SSAT((int32_t)hist_x[k] - v, 16);
}

/* Filtra una muestra con el filtro activo */
static q15_t filtrar(q15_t x) {
	q15_t y;
	switch (tipo_activo) {
	case PB:
		arm_fir_q15(fir_fs_activo, &x, &y, BLOCK_SIZE);
		break;
	case PBANDA:
		x = complemento_multitasa(x);                    /* PA ... */
		arm_fir_q15(fir_fs_activo, &x, &y, BLOCK_SIZE);  /* ... y despues LPB */
		break;
	default:                                           /* PA y EB */
		y = complemento_multitasa(x);
		break;
	}
	return y;
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
 * filtro nunca ocurre en medio de un calculo. */
static void aplicar_seleccion(void) {
	if (!filtros_listos) {
		return;                       /* se aplica cuando termine init_filtros() */
	}
	int fila = FILA_FILTRO(state_counter);
	int f    = filter_counter;
	cambio_filtro = false;

	tipo_activo   = f;
	fir_fs_activo = (f == PB) ? &fir_pb[fila] : &fir_lpb[fila];
	camino.hd = hd_tabla[fila];
	camino.m  = m_tabla[fila];
	camino.l  = hd_pad[fila] / m_tabla[fila];
	camino.d  = (f == EB) ? d_eb_tabla[fila] : d_pa_tabla[fila];
	camino.g  = (f == EB) ? &fir_g[1] : &fir_g[0];

	/* Sin historia del filtro anterior */
	memset(estado_fs, 0, sizeof(estado_fs));
	memset(estado_g,  0, sizeof(estado_g));
	memset(hist_x,    0, sizeof(hist_x));
	memset(acc_hd,    0, sizeof(acc_hd));
	memset(hist_w,    0, sizeof(hist_w));
	pos_x  = 0;
	fase   = 0;
	ranura = 0;
}

/* Configura P0_26 como salida GPIO para medir la duracion de la ISR. */
static void init_pin_medicion(void) {
#if MEDIR_TIEMPO_ISR
	gpio_pin_config_t cfg = { kGPIO_DigitalOutput, 0U };
	CLOCK_EnableClock(kCLOCK_Port0);
	CLOCK_EnableClock(kCLOCK_Gpio0);
	PORT_SetPinMux(PORT0, PIN_MEDICION, kPORT_MuxAlt0);
	GPIO_PinInit(GPIO0, PIN_MEDICION, &cfg);
#endif
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

  filter_counter = (filter_counter + 1) % (NUM_TIPOS);
  cambio_filtro = true;

  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* GPIO10_IRQn interrupt handler: salida filtrada / sin filtrar */
void GPIO1_INT_0_IRQHANDLER(void) {
  uint32_t pin_flags = GPIO_GpioGetInterruptChannelFlags(GPIO1, 0U);

  salida_filtrada = !salida_filtrada;

  GPIO_GpioClearInterruptChannelFlags(GPIO1, pin_flags, 0U);
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

/* ADC0_IRQn interrupt handler: una muestra por interrupcion */
void ADC0_IRQHANDLER(void) {
#if MEDIR_TIEMPO_ISR
  GPIO_PortSet(GPIO0, 1UL << PIN_MEDICION);      /* inicio de la ISR */
#endif
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

    /* El pin de GPIO1 elige que va al DAC; el filtro solo corre si es la
     * salida filtrada (y despues de init_filtros()) */
    if (salida_filtrada && filtros_listos) {
      buffer_out[index_buffer] = filtrar(buffer_in[index_buffer]);
    }
    q15_t salida = salida_filtrada ? buffer_out[index_buffer] : buffer_in[index_buffer];

    /* Q15 -> 12 bits para el DAC */
    int32_t out_dac = ((int32_t)salida / 16) + ADC_MID;
    if (out_dac < 0)    out_dac = 0;
    if (out_dac > 4095) out_dac = 4095;
    DAC_SetData(DAC0, (uint16_t)out_dac);

    index_buffer = (index_buffer + 1) % BUFFER_SIZE;
  }

#if MEDIR_TIEMPO_ISR
  GPIO_PortClear(GPIO0, 1UL << PIN_MEDICION);    /* fin de la ISR */
#endif

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
    init_pin_medicion();
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
