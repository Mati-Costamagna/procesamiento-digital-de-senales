/*
 * TP4 - Procesamiento Digital de Señales
 * Filtros adaptativos (LMS) sobre FRDM-MCXN947
 *
 * app_config.h: todos los parámetros "tocables" del laboratorio en un solo lugar.
 */
#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Puerto serie: VCOM del MCU-Link (USB de debug, J17) = LP_FLEXCOMM4
 * (P1_8 = RX, P1_9 = TX). En la PC aparece como "MCU-Link VCom Port (COMx)".
 * Se puede abrir con cualquier terminal (PuTTY, Tera Term, la terminal de
 * MCUXpresso) o con los scripts de MATLAB de TP4/matlab.
 * ------------------------------------------------------------------------- */
#define APP_UART            LPUART4
#define APP_UART_BAUD       115200U
#define APP_UART_CLK_FREQ   12000000U   /* FRO 12 MHz -> FLEXCOMM4 */

/* ---------------------------------------------------------------------------
 * Parte 1: identificación de planta (no usa ADC ni DAC)
 * ------------------------------------------------------------------------- */
#define APP_P1_N_DEFAULT    20000U      /* muestras por experimento (200 bloques) */
#define APP_P1_BLOCK        100U        /* MSE promediado cada 100 muestras (consigna) */
/* Valores de mu que se recorren con SW3 y con el comando B (barrido) */
#define APP_P1_MU_LIST      {0.002f, 0.01f, 0.05f, 0.1f, 0.19f, 0.21f}

/* ---------------------------------------------------------------------------
 * Parte 2: cancelador de ruido. ADC0 canal A2 = P4_23 (J8 pin 28).
 * Rango de entrada 0 .. 3,3 V -> la senoidal tiene que venir montada sobre
 * ~1,65 V de continua. La continua se estima y se resta en el firmware.
 * ------------------------------------------------------------------------- */
#define APP_ADC             ADC0
#define APP_ADC_IRQn        ADC0_IRQn
#define APP_ADC_IRQHandler  ADC0_IRQHandler
#define APP_ADC_CHANNEL     2U
#define APP_ADC_CMDID       1U
#define APP_ADC_VREF_V      3.3f
#define APP_ADC_FULL_SCALE  65536.0f    /* 16 bits */
#define APP_DC_ALPHA        (1.0f / 4096.0f) /* seguimiento lento de la continua */

#define APP_P2_FS_DEFAULT   8000U       /* frecuencia de muestreo de la parte 2 */
#define APP_FS_MIN          1000U
#define APP_FS_MAX          48000U

/* Base de tiempo de muestreo: CTIMER0, match 0 + reset (igual que en el TP3) */
#define APP_CTIMER          CTIMER0
#define APP_CTIMER_IRQn     CTIMER0_IRQn
#define APP_CTIMER_ID       0U

/* Salida al osciloscopio: DAC0 (12 bits, 0..3,3 V) en P4_2 = J1 pin 4.
 * La señal elegida (d, e, y, s o residuo) sale centrada en APP_DAC_MID_V. */
#define APP_DAC             DAC0
#define APP_DAC_FULL_SCALE  4095.0f
#define APP_DAC_MID_V       1.65f

/* Pines de medición (salidas digitales 0 / 3,3 V) */
#define APP_PIN_TRIG_GPIO   GPIO1       /* P1_23 = J1 pin 8 : pulso al reiniciar los coeficientes */
#define APP_PIN_TRIG_PIN    23U
#define APP_PIN_PROC_GPIO   GPIO4       /* P4_0  = J2 pin 18: en alto mientras se procesa una muestra */
#define APP_PIN_PROC_PIN    0U
#define APP_TRIG_WIDTH      8U          /* ancho del pulso de TRIG, en muestras */

/* Teclas (activas en bajo) */
#define APP_SW2_GPIO        GPIO0       /* SW2 */
#define APP_SW2_PIN         23U
#define APP_SW3_GPIO        GPIO0       /* SW3 */
#define APP_SW3_PIN         6U
#define APP_LONG_PRESS_MS   700U
#define APP_DEBOUNCE_MS     30U

/* LED RGB (activos en bajo) */
#define APP_LED_RED_GPIO    GPIO0
#define APP_LED_RED_PIN     10U
#define APP_LED_GREEN_GPIO  GPIO0
#define APP_LED_GREEN_PIN   27U
#define APP_LED_BLUE_GPIO   GPIO1
#define APP_LED_BLUE_PIN    2U

#endif /* APP_CONFIG_H_ */
