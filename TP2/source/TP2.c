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
 * Touchpad E1:      (TSI0 canal 3, P1_3) cada toque alterna entre filtrar y
 *                   hacer bypass (copiar la muestra del ADC al buffer de
 *                   salida, sin calcular el filtro). El DAC lee siempre del
 *                   buffer de salida.
 * LED:              color = fs. Con la salida filtrada parpadea a 1, 2, 4 u
 *                   8 Hz (PB, PA, PBanda, EB); sin filtrar queda fijo.
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
#include "fsl_tsi_v6.h"
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

/* Pin de medicion del tiempo de la ISR del ADC (libre en la placa) */
#define MEDIR_TIEMPO_ISR 1     /* 0 = desactivado */
#define PIN_MEDICION     26U   /* P0_26 -> J2[10] */

/* Touchpad E1 de la placa: TSI0 canal 3 en P1_3 (BOARD_TSI_ELECTRODE_1 del
 * ejemplo tsi_v6 del SDK). Se lee desde el lazo principal, sin interrupciones. */
#define TSI_CANAL        3U
#define TSI_PIN          3U     /* P1_3 */
#define TOQUE_DELTA      20     /* cuentas debajo de la base que son un toque */
#define TOQUE_LECTURAS   3U     /* lecturas seguidas para aceptar un cambio */
#define PERIODO_TSI_MS   10U    /* un barrido del touchpad cada 10 ms */
#define TIMEOUT_TSI_MS   50U    /* si un barrido no termina, se desactiva el TSI */
#define LECTURAS_BASE    16U    /* barridos promediados para la base, sin tocar */
#define DEPURAR_TOUCH    0      /* 1 = imprime la cuenta del TSI cada 200 ms */

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

/* LED: el color indica la fs; con la salida filtrada parpadea con un
 * semiperiodo distinto para cada filtro, y sin filtrar queda fijo. */
static const uint16_t semiperiodo_led_ms[NUM_TIPOS] = {
	500,   /* PB:     1 Hz */
	250,   /* PA:     2 Hz */
	125,   /* PBanda: 4 Hz */
	62     /* EB:     8 Hz */
};

static volatile uint32_t ms_ticks;    /* base de tiempo de 1 ms (SysTick) */

/* Estado del touchpad (solo lo usa el lazo principal) */
static bool     touch_ok;             /* false si el TSI no responde */
static bool     touch_midiendo;       /* hay un barrido en curso */
static bool     touch_tocado;         /* estado con antirrebote */
static uint8_t  touch_cuenta;         /* lecturas seguidas en el estado opuesto */
static int32_t  touch_base;           /* cuenta del TSI sin tocar */
static uint32_t touch_t0;             /* inicio del ultimo barrido */

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
		PRINTF("Error inicializando %s a %u Hz\r\n", nombre, (unsigned)fs);
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

static void led_apagar(void) {
	GPIO_PinWrite(GPIO0, LED_RED_PIN, 1);
	GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 1);
	GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
}

/* Lazo principal: el color indica la fs (set_state) y el parpadeo, el
 * filtro. Con la salida sin filtrar el LED queda fijo. Solo el lazo
 * principal escribe el LED. */
static void led_actualizar(uint32_t ahora) {
	static bool     parpadeando = false;
	static bool     encendido   = true;
	static uint32_t t_cambio    = 0;
	static int      mostrado    = -2;   /* lo que hay en el LED: fs, o -1 apagado */

	bool parpadear = salida_filtrada && filtros_listos;
	if (parpadear != parpadeando) {      /* empieza o termina el parpadeo */
		parpadeando = parpadear;
		encendido   = true;
		t_cambio    = ahora;
	} else if (parpadear && (ahora - t_cambio >= semiperiodo_led_ms[filter_counter])) {
		encendido = !encendido;
		t_cambio  = ahora;
	}

	int estado = encendido ? state_counter : -1;
	if (estado != mostrado) {
		if (encendido) {
			set_state(state_counter);
		} else {
			led_apagar();
		}
		mostrado = estado;
	}
}

/* Base de tiempo de 1 ms para el touchpad y el LED (prioridad minima, asi
 * que nunca demora a la rutina del ADC). */
void SysTick_Handler(void) {
	ms_ticks++;
}

/* Un barrido del touchpad con espera activa (solo para medir la base). */
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

/* TSI0 en modo self-cap sobre el electrodo E1, con disparo por software. La
 * base se mide al arrancar: no hay que tocar el touchpad durante el reset. */
static void init_touch(void) {
	const port_pin_config_t pin_tsi = {
		.mux         = kPORT_MuxAlt0,             /* TSI0_CH3 */
		.inputBuffer = kPORT_InputBufferDisable,  /* funcion analogica */
	};
	tsi_selfCap_config_t cfg;
	uint32_t suma = 0;
	uint16_t cuenta;

	CLOCK_EnableClock(kCLOCK_Port1);
	PORT_SetPinConfig(PORT1, TSI_PIN, &pin_tsi);
	CLOCK_SetClkDiv(kCLOCK_DivTsiClk, 1U);
	CLOCK_AttachClk(kFRO12M_to_TSI);      /* clk_in (cristal) no esta habilitado */

	TSI_GetSelfCapModeDefaultConfig(&cfg);
	TSI_InitSelfCapMode(TSI0, &cfg);
	TSI_EnableNoiseCancellation(TSI0, false);
	TSI_EnableHardwareTriggerScan(TSI0, false);
	TSI_DisableInterrupts(TSI0, kTSI_GlobalInterruptEnable | kTSI_EndOfScanInterruptEnable);
	TSI_SetSelfCapMeasuredChannel(TSI0, TSI_CANAL);
	TSI_EnableModule(TSI0, true);

	for (uint32_t i = 0; i < LECTURAS_BASE; i++) {
		if (!tsi_barrido_bloqueante(&cuenta)) {
			PRINTF("Touchpad: el TSI no responde, queda desactivado\r\n");
			return;
		}
		suma += cuenta;
	}
	touch_base = (int32_t)(suma / LECTURAS_BASE);
	touch_t0   = ms_ticks;
	touch_ok   = true;
	PRINTF("Touchpad: base de %d cuentas\r\n", (int)touch_base);
}

/* Antirrebote con histeresis. Devuelve true solo en el instante en que se
 * confirma un toque nuevo (el paso de "sin tocar" a "tocado"). */
static bool touch_nuevo_toque(uint16_t cuenta) {
	int32_t umbral = touch_tocado ? touch_base - TOQUE_DELTA / 2   /* para soltar */
	                              : touch_base - TOQUE_DELTA;      /* para tocar */
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

/* Lazo principal: un barrido cada PERIODO_TSI_MS, sin esperas. Cada toque
 * nuevo alterna entre la salida filtrada y la senal sin filtrar. */
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
			PRINTF("Touchpad: el TSI no responde, queda desactivado\r\n");
		}
		return;
	}
	uint16_t cuenta = TSI_GetCounter(TSI0);
	TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);
	touch_midiendo = false;

#if DEPURAR_TOUCH
	static uint32_t t_impresion;
	if (ahora - t_impresion >= 200U) {
		t_impresion = ahora;
		PRINTF("TSI: %u (base %d)\r\n", (unsigned)cuenta, (int)touch_base);
	}
#endif

	if (touch_nuevo_toque(cuenta)) {
		if (!salida_filtrada) {
			cambio_filtro = true;       /* el filtro arranca sin historia vieja */
		}
		salida_filtrada = !salida_filtrada;
	}
}

/* GPIO00_IRQn interrupt handler: cambia la frecuencia de muestreo */
void GPIO0_INT_0_IRQHANDLER(void) {
  uint32_t pin_flags0 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);

  state_counter = (state_counter + 1) % NUM_FS_MUESTREO;
  setup_new_match(sample_vel[state_counter]);
  cambio_filtro = true;          /* hay que usar los coeficientes de la nueva fs */
                                 /* el color del LED lo cambia el lazo principal */

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

/* GPIO10_IRQn: P1_23 ya no se usa (la habilitacion del filtro paso al
 * touchpad). main() deshabilita la interrupcion; el handler solo limpia la
 * bandera por si llega a dispararse antes, mientras siga configurada en
 * Config Tools. */
void GPIO1_INT_0_IRQHANDLER(void) {
  uint32_t pin_flags = GPIO_GpioGetInterruptChannelFlags(GPIO1, 0U);

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

    /* El touchpad elige si se filtra (solo despues de init_filtros()). En
     * bypass la muestra se copia al buffer de salida: el DAC lee siempre de
     * ese buffer, como pide la consigna. */
    if (salida_filtrada && filtros_listos) {
      buffer_out[index_buffer] = filtrar(buffer_in[index_buffer]);
    } else {
      buffer_out[index_buffer] = buffer_in[index_buffer];      /* bypass */
    }

    /* Q15 -> 12 bits para el DAC */
    int32_t out_dac = ((int32_t)buffer_out[index_buffer] / 16) + ADC_MID;
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
#ifdef GPIO1_INT_0_IRQN
    DisableIRQ(GPIO1_INT_0_IRQN);   /* P1_23 ya no se usa (ver su handler) */
#endif
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

    init_filtros();
    cambio_filtro = true;

    SysTick_Config(SystemCoreClock / 1000U);   /* base de tiempo de 1 ms */
    PRINTF("TP2 - FIR por muestras\r\n");
    init_touch();

    /* Lazo principal: touchpad y LED. Lo interrumpe la rutina del ADC. */
    while (1) {
        uint32_t ahora = ms_ticks;
        touch_actualizar(ahora);
        led_actualizar(ahora);
    }
    return 0 ;
}
