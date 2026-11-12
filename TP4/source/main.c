/*
 * TP4 / Laboratorio 4 - Procesamiento Digital de Señales
 * Filtros adaptativos (LMS) en la FRDM-MCXN947 (Cortex-M33 con FPU y DSP).
 *
 * PARTE 1 - Identificación de planta (parte1.c). No usa ADC ni DAC: la placa
 *   simula la planta (FIR de 30 coeficientes fijos), la entrada (aleatoria o
 *   constante) y el filtro adaptativo LMS de 30 coeficientes. Cada 100
 *   muestras manda por el puerto serie el MSE promedio y la diferencia entre
 *   los coeficientes de la planta y los del filtro.
 *
 * PARTE 2 - Cancelación de ruido (parte2.c). CTIMER0 marca fs; en cada tick
 *   se dispara el ADC y en la ISR del ADC: s = senoidal del generador,
 *   se le suma ruido pseudoaleatorio pasado por el FIR correlador, el LMS
 *   cancela el ruido y por el DAC sale la señal elegida para el osciloscopio.
 *
 * Teclas:
 *   SW2          Parte 1: arranca un experimento (o lo corta si está corriendo)
 *                Parte 2: pone los coeficientes en cero (re-aprende) + pulso en TRIG
 *   SW3 corto    Parte 1: siguiente mu de la lista
 *                Parte 2: cambia la señal del DAC: d -> e -> y -> s -> residuo
 *   SW3 largo    cambia de Parte 1 a Parte 2 y viceversa
 * LEDs: verde = parte 1, rojo = parte 2, azul = cambia con cada bloque enviado.
 *
 * Comandos por el puerto serie (una letra + valor, terminados en Enter).
 * Escribir ? para ver la lista y la configuración actual.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_ctimer.h"
#include "fsl_dac.h"
#include "fsl_gpio.h"
#include "fsl_lpadc.h"
#include "fsl_lpuart.h"
#include "fsl_spc.h"
#include "fsl_vref.h"

#include "clock_config.h"

#include "arm_math.h"

#include "app_config.h"
#include "app_pins.h"
#include "coefs.h"
#include "lms.h"
#include "parte1.h"
#include "parte2.h"
#include "txt_out.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/
static const float s_p1_mu_list[] = APP_P1_MU_LIST;
#define P1_MU_CNT (sizeof(s_p1_mu_list) / sizeof(s_p1_mu_list[0]))

static volatile uint32_t s_part = 1U;   /* 1 o 2 */

/* Parte 1 */
static p1_cfg_t s_p1;
static uint32_t s_p1_mu_idx = 1U;       /* 0,01 */
static bool s_p1_start;
static bool s_p1_sweep;

/* Parte 2 */
static p2_cfg_t s_p2;
static uint32_t s_p2_fs = APP_P2_FS_DEFAULT;
static float s_p2_fs_real;
static volatile p2_out_t s_dac_sel = P2_OUT_E;
static volatile float s_dc        = 1.65f;
static volatile bool s_p2_running;
static volatile bool s_p2_reset_req;
static volatile uint32_t s_trig_cnt;
static bool s_p2_print = true;
static uint32_t s_p2_print_every = 1U;
static uint32_t s_p2_print_cnt;

/* Bloques de estadísticas de la ISR hacia el lazo principal (FIFO) */
#define STATS_FIFO 16U
static p2_stats_t s_stats_fifo[STATS_FIFO];
static volatile uint32_t s_stats_wr;
static volatile uint32_t s_stats_rd;
static volatile uint32_t s_stats_lost;

/* Tiempo de procesamiento por muestra (ISR), en ciclos */
static volatile uint32_t s_cyc_max;
static volatile uint32_t s_cyc_acc;
static volatile uint32_t s_cyc_n;

/* SysTick y teclas */
static volatile uint32_t s_ms;
static volatile bool s_ev_sw2;
static volatile bool s_ev_sw3_short;
static volatile bool s_ev_sw3_long;

/* UART */
static lpuart_handle_t s_uart_handle;
static uint8_t s_uart_ring[64];
static char s_cmd[24];
static uint32_t s_cmd_len;

/*******************************************************************************
 * Utilidades
 ******************************************************************************/
static uint32_t cycles_now(void)
{
    return DWT->CYCCNT;
}

static void cycle_counter_init(void)
{
#if defined(DCB)
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
#else
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#endif
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void uart_write(const char *s, size_t len)
{
    (void)LPUART_WriteBlocking(APP_UART, (const uint8_t *)s, len);
}

static void uart_puts(const char *s)
{
    uart_write(s, strlen(s));
}

static void leds_update(void)
{
    /* activos en bajo */
    GPIO_PinWrite(APP_LED_GREEN_GPIO, APP_LED_GREEN_PIN, (s_part == 1U) ? 0U : 1U);
    GPIO_PinWrite(APP_LED_RED_GPIO, APP_LED_RED_PIN, (s_part == 2U) ? 0U : 1U);
}

/*******************************************************************************
 * Teclas: antirrebote y pulsación larga en el SysTick (1 ms)
 ******************************************************************************/
typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
    uint8_t stable; /* 1 = suelta (pull-up), 0 = apretada */
    uint8_t cnt;
    uint32_t t_press;
} button_t;

static button_t s_sw2 = {APP_SW2_GPIO, APP_SW2_PIN, 1U, 0U, 0U};
static button_t s_sw3 = {APP_SW3_GPIO, APP_SW3_PIN, 1U, 0U, 0U};

/* 0 nada, 1 se soltó tras pulsación corta, 2 tras pulsación larga */
static uint8_t button_tick(button_t *b, uint32_t now)
{
    uint8_t raw = (uint8_t)GPIO_PinRead(b->gpio, b->pin);
    if (raw == b->stable)
    {
        b->cnt = 0U;
        return 0U;
    }
    if (++b->cnt < APP_DEBOUNCE_MS)
    {
        return 0U;
    }
    b->cnt    = 0U;
    b->stable = raw;
    if (raw == 0U)
    {
        b->t_press = now;
        return 0U;
    }
    return ((now - b->t_press) >= APP_LONG_PRESS_MS) ? 2U : 1U;
}

void SysTick_Handler(void)
{
    uint32_t now = ++s_ms;
    if (button_tick(&s_sw2, now) != 0U)
    {
        s_ev_sw2 = true;
    }
    uint8_t r3 = button_tick(&s_sw3, now);
    if (r3 == 1U)
    {
        s_ev_sw3_short = true;
    }
    else if (r3 == 2U)
    {
        s_ev_sw3_long = true;
    }
}

/*******************************************************************************
 * Periféricos (mismos que el TP3)
 ******************************************************************************/
static void uart_cb(LPUART_Type *base, lpuart_handle_t *handle, status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)status;
    (void)userData;
}

static void uart_init(void)
{
    lpuart_config_t cfg;

    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1U);
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM4);

    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = APP_UART_BAUD;
    cfg.enableTx     = true;
    cfg.enableRx     = true;
    (void)LPUART_Init(APP_UART, &cfg, APP_UART_CLK_FREQ);

    LPUART_TransferCreateHandle(APP_UART, &s_uart_handle, uart_cb, NULL);
    LPUART_TransferStartRingBuffer(APP_UART, &s_uart_handle, s_uart_ring, sizeof(s_uart_ring));
}

static void adc_init(void)
{
    lpadc_config_t adc_cfg;
    lpadc_conv_command_config_t cmd_cfg;
    lpadc_conv_trigger_config_t trg_cfg;
    vref_config_t vref_cfg;

    CLOCK_SetClkDiv(kCLOCK_DivAdc0Clk, 1U);
    CLOCK_AttachClk(kFRO_HF_to_ADC0);

    /* El VREF tiene que estar prendido: polariza al LPADC */
    SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlVref);
    VREF_GetDefaultConfig(&vref_cfg);
    vref_cfg.bufferMode = kVREF_ModeBandgapOnly;
    VREF_Init(VREF0, &vref_cfg);

    LPADC_GetDefaultConfig(&adc_cfg);
    adc_cfg.enableAnalogPreliminary = true;
    adc_cfg.referenceVoltageSource  = kLPADC_ReferenceVoltageAlt3; /* VDD_ANA = 3,3 V */
#if defined(FSL_FEATURE_LPADC_HAS_CFG_PWRSEL) && (FSL_FEATURE_LPADC_HAS_CFG_PWRSEL == 1U)
    adc_cfg.powerLevelMode = kLPADC_PowerLevelAlt4;
#endif
    LPADC_Init(APP_ADC, &adc_cfg);

#if defined(FSL_FEATURE_LPADC_HAS_CTRL_CALOFSMODE) && FSL_FEATURE_LPADC_HAS_CTRL_CALOFSMODE
    LPADC_SetOffsetCalibrationMode(APP_ADC, kLPADC_OffsetCalibration16bitMode);
#endif
#if defined(FSL_FEATURE_LPADC_HAS_CTRL_CALOFS) && FSL_FEATURE_LPADC_HAS_CTRL_CALOFS
    LPADC_DoOffsetCalibration(APP_ADC);
#endif
#if defined(FSL_FEATURE_LPADC_HAS_CTRL_CAL_REQ) && FSL_FEATURE_LPADC_HAS_CTRL_CAL_REQ
    SDK_DelayAtLeastUs(1U, SystemCoreClock);
    LPADC_DoAutoCalibration(APP_ADC);
#endif

    /* Comando 1: canal A2, single-ended, 16 bits */
    LPADC_GetDefaultConvCommandConfig(&cmd_cfg);
    cmd_cfg.channelNumber            = APP_ADC_CHANNEL;
    cmd_cfg.conversionResolutionMode = kLPADC_ConversionResolutionHigh;
    LPADC_SetConvCommandConfig(APP_ADC, APP_ADC_CMDID, &cmd_cfg);

    /* Trigger 0 -> comando 1, por software (lo dispara la ISR del CTIMER) */
    LPADC_GetDefaultConvTriggerConfig(&trg_cfg);
    trg_cfg.targetCommandId       = APP_ADC_CMDID;
    trg_cfg.enableHardwareTrigger = false;
    LPADC_SetConvTriggerConfig(APP_ADC, 0U, &trg_cfg);

    LPADC_EnableInterrupts(APP_ADC, kLPADC_FIFO0WatermarkInterruptEnable);
    NVIC_SetPriority(APP_ADC_IRQn, 1U);
    (void)EnableIRQ(APP_ADC_IRQn);
}

static void dac_init(void)
{
    dac_config_t cfg;

    CLOCK_SetClkDiv(kCLOCK_DivDac0Clk, 1U);
    CLOCK_AttachClk(kFRO_HF_to_DAC0);
    SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlDac0);

    DAC_GetDefaultConfig(&cfg);
    cfg.referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt1; /* VDD_ANA = 3,3 V */
    DAC_Init(APP_DAC, &cfg);
    DAC_Enable(APP_DAC, true);
    DAC_SetData(APP_DAC, 2048U);
}

static inline void dac_write_volts(float v)
{
    float c = (v + APP_DAC_MID_V) * (APP_DAC_FULL_SCALE / APP_ADC_VREF_V);
    if (c < 0.0f)
    {
        c = 0.0f;
    }
    else if (c > APP_DAC_FULL_SCALE)
    {
        c = APP_DAC_FULL_SCALE;
    }
    DAC_SetData(APP_DAC, (uint32_t)(c + 0.5f));
}

static void gpio_init(void)
{
    const gpio_pin_config_t in       = {kGPIO_DigitalInput, 0U};
    const gpio_pin_config_t led_off  = {kGPIO_DigitalOutput, 1U};
    const gpio_pin_config_t test_out = {kGPIO_DigitalOutput, 0U};

    GPIO_PinInit(APP_SW2_GPIO, APP_SW2_PIN, &in);
    GPIO_PinInit(APP_SW3_GPIO, APP_SW3_PIN, &in);
    GPIO_PinInit(APP_LED_RED_GPIO, APP_LED_RED_PIN, &led_off);
    GPIO_PinInit(APP_LED_GREEN_GPIO, APP_LED_GREEN_PIN, &led_off);
    GPIO_PinInit(APP_LED_BLUE_GPIO, APP_LED_BLUE_PIN, &led_off);
    GPIO_PinInit(APP_PIN_TRIG_GPIO, APP_PIN_TRIG_PIN, &test_out);
    GPIO_PinInit(APP_PIN_PROC_GPIO, APP_PIN_PROC_PIN, &test_out);
}

/*******************************************************************************
 * Parte 2: muestreo y procesamiento en interrupciones
 ******************************************************************************/

/* CTIMER0 a fs: dispara una conversión del ADC */
static void ctimer_match_cb(uint32_t flags)
{
    (void)flags;
    LPADC_DoSoftwareTrigger(APP_ADC, 1U); /* máscara 1 = trigger 0 */
}

static ctimer_callback_t s_ctimer_cb[] = {ctimer_match_cb};

static void ctimer_init(void)
{
    ctimer_config_t cfg;

    CLOCK_SetClkDiv(kCLOCK_DivCtimer0Clk, 1U);
    CLOCK_AttachClk(kFRO_HF_to_CTIMER0);

    CTIMER_GetDefaultConfig(&cfg);
    CTIMER_Init(APP_CTIMER, &cfg);
    CTIMER_RegisterCallBack(APP_CTIMER, &s_ctimer_cb[0], kCTIMER_SingleCallback);
    NVIC_SetPriority(APP_CTIMER_IRQn, 0U);
}

/* Arranca el CTIMER a la fs pedida y devuelve la fs real */
static float ctimer_start(uint32_t fs)
{
    ctimer_match_config_t m;
    const uint32_t clk = CLOCK_GetCTimerClkFreq(APP_CTIMER_ID);
    uint32_t div       = (clk + fs / 2U) / fs;

    if (div < 2U)
    {
        div = 2U;
    }
    CTIMER_StopTimer(APP_CTIMER);
    m.matchValue         = div - 1U;
    m.enableCounterReset = true;
    m.enableCounterStop  = false;
    m.outControl         = kCTIMER_Output_NoAction;
    m.outPinInitState    = false;
    m.enableInterrupt    = true;
    CTIMER_SetupMatch(APP_CTIMER, kCTIMER_Match_0, &m);
    (void)EnableIRQ(APP_CTIMER_IRQn);
    CTIMER_Reset(APP_CTIMER);
    CTIMER_StartTimer(APP_CTIMER);

    return (float)clk / (float)div;
}

static void ctimer_stop(void)
{
    CTIMER_StopTimer(APP_CTIMER);
}

/* Una muestra de la parte 2 por cada conversión del ADC */
void APP_ADC_IRQHandler(void)
{
    lpadc_conv_result_t r;
    while (LPADC_GetConvResult(APP_ADC, &r, 0U))
    {
        if (!s_p2_running)
        {
            continue;
        }
        GPIO_PortSet(APP_PIN_PROC_GPIO, 1UL << APP_PIN_PROC_PIN);
        const uint32_t c0 = cycles_now();

        if (s_p2_reset_req)
        {
            s_p2_reset_req = false;
            p2_reset_w();
            s_trig_cnt = APP_TRIG_WIDTH;
            GPIO_PortSet(APP_PIN_TRIG_GPIO, 1UL << APP_PIN_TRIG_PIN);
        }

        /* cuentas -> volts, y se saca la continua (offset del generador) */
        const float v = (float)r.convValue * (APP_ADC_VREF_V / APP_ADC_FULL_SCALE);
        float dc      = s_dc;
        dc += (v - dc) * APP_DC_ALPHA;
        s_dc = dc;

        float out[P2_OUT_CNT];
        const bool blk = p2_process(v - dc, out);
        dac_write_volts(out[s_dac_sel]);

        if (blk)
        {
            const uint32_t w = s_stats_wr;
            if ((w - s_stats_rd) < STATS_FIFO)
            {
                p2_get_stats(&s_stats_fifo[w % STATS_FIFO]);
                s_stats_wr = w + 1U;
            }
            else
            {
                s_stats_lost++;
            }
        }

        if (s_trig_cnt > 0U)
        {
            if (--s_trig_cnt == 0U)
            {
                GPIO_PortClear(APP_PIN_TRIG_GPIO, 1UL << APP_PIN_TRIG_PIN);
            }
        }

        const uint32_t dc_cyc = cycles_now() - c0;
        s_cyc_acc += dc_cyc;
        s_cyc_n++;
        if (dc_cyc > s_cyc_max)
        {
            s_cyc_max = dc_cyc;
        }
        GPIO_PortClear(APP_PIN_PROC_GPIO, 1UL << APP_PIN_PROC_PIN);
    }
    SDK_ISR_EXIT_BARRIER;
}

static void p2_start(void)
{
    s_p2_running = false;
    ctimer_stop();
    p2_init(&s_p2);
    s_stats_rd   = s_stats_wr;
    s_cyc_max    = 0U;
    s_cyc_acc    = 0U;
    s_cyc_n      = 0U;
    s_p2_fs_real = ctimer_start(s_p2_fs);
    p2_print_header(uart_write, s_p2_fs_real);
    s_p2_running = true;
}

static void p2_stop(void)
{
    s_p2_running = false;
    ctimer_stop();
    DAC_SetData(APP_DAC, 2048U);
    GPIO_PortClear(APP_PIN_TRIG_GPIO, 1UL << APP_PIN_TRIG_PIN);
}

static void p2_print_timing(void)
{
    txt_line_t ln;
    const uint32_t n   = s_cyc_n;
    const uint32_t avg = (n > 0U) ? (s_cyc_acc / n) : 0U;
    txt_begin(&ln, "P2T");
    txt_kv_u32(&ln, "cyc_prom", avg);
    txt_kv_u32(&ln, "cyc_max", s_cyc_max);
    txt_kv_u32(&ln, "f_cpu", SystemCoreClock);
    txt_kv_f32(&ln, "us_prom", (float)avg * 1.0e6f / (float)SystemCoreClock);
    txt_kv_f32(&ln, "us_disp", 1.0e6f / s_p2_fs_real);
    txt_kv_u32(&ln, "bloques_perdidos", s_stats_lost);
    txt_end(&ln, uart_write);
}

/*******************************************************************************
 * Comandos y estado
 ******************************************************************************/
static void print_help(void)
{
    txt_line_t ln;

    uart_puts("\r\n# TP4 PDS - Filtros adaptativos LMS - FRDM-MCXN947\r\n"
              "# Comandos (letra + valor + Enter):\r\n"
              "#   1 / 2      pasar a la Parte 1 / Parte 2\r\n"
              "#   U<mu>      paso de adaptacion (ej. U0.01)\r\n"
              "#   G<0|1|2>   algoritmo: 0 LMS de clase, 1 arm_lms_f32, 2 arm_lms_norm_f32 (NLMS)\r\n"
              "#   A<amp>     P1: amplitud de la entrada | P2: amplitud del ruido (V)\r\n"
              "#   P1: E<0|1> entrada aleatoria/constante, V<sigma> ruido de medicion,\r\n"
              "#       N<muestras>, R<semilla>, D<0|1> mandar diferencias, S arrancar,\r\n"
              "#       B barrido de mu, X cortar\r\n"
              "#   P2: Q<0|1> correlador en el ruido/en la referencia, F<hz> fs,\r\n"
              "#       O<0..4> DAC: d, e, y, s, residuo; Z reiniciar coeficientes,\r\n"
              "#       C ver coeficientes, T tiempos, M<k> imprimir 1 de cada k bloques (0 = no)\r\n");

    txt_begin(&ln, "#CFG");
    txt_kv_u32(&ln, "parte", s_part);
    if (s_part == 1U)
    {
        txt_kv_str(&ln, "alg", lms_alg_name(s_p1.alg));
        txt_kv_f32(&ln, "mu", s_p1.mu);
        txt_kv_str(&ln, "in", (s_p1.input == P1_IN_CONST) ? "CONST" : "RANDOM");
        txt_kv_f32(&ln, "A", s_p1.amp);
        txt_kv_f32(&ln, "sv", s_p1.sigma_v);
        txt_kv_u32(&ln, "N", s_p1.n_total);
        txt_kv_u32(&ln, "seed", s_p1.seed);
        txt_kv_f32(&ln, "mu_max", p1_mu_max(&s_p1));
    }
    else
    {
        txt_kv_str(&ln, "alg", lms_alg_name(s_p2.alg));
        txt_kv_f32(&ln, "mu", s_p2.mu);
        txt_kv_f32(&ln, "ruido", s_p2.noise_amp);
        txt_kv_str(&ln, "topo", (s_p2.topo == P2_CORR_EN_RUIDO) ? "RUIDO" : "REFERENCIA");
        txt_kv_f32(&ln, "fs", s_p2_fs_real);
        txt_kv_str(&ln, "dac", p2_out_name(s_dac_sel));
        txt_kv_f32(&ln, "dc", s_dc);
    }
    txt_end(&ln, uart_write);
}

static void set_part(uint32_t part)
{
    if (part == s_part)
    {
        return;
    }
    s_part = part;
    if (part == 2U)
    {
        p2_start();
    }
    else
    {
        p2_stop();
    }
    leds_update();
    print_help();
}

static void process_command(const char *cmd)
{
    const char c  = (char)((cmd[0] >= 'a' && cmd[0] <= 'z') ? (cmd[0] - 32) : cmd[0]);
    const float v = (float)strtod(&cmd[1], NULL);
    const long iv = strtol(&cmd[1], NULL, 10);
    bool p2_restart = false;

    switch (c)
    {
        case '1':
            set_part(1U);
            return;
        case '2':
            set_part(2U);
            return;
        case '?':
        case 'H':
            print_help();
            return;
        case 'U':
            if (v > 0.0f)
            {
                if (s_part == 1U)
                {
                    s_p1.mu = v;
                }
                else
                {
                    s_p2.mu = v;
                    p2_set_mu(v); /* sin borrar lo aprendido */
                    p2_print_header(uart_write, s_p2_fs_real);
                }
            }
            break;
        case 'G':
            if ((iv >= 0) && (iv <= 2))
            {
                if (s_part == 1U)
                {
                    s_p1.alg = (lms_alg_t)iv;
                }
                else
                {
                    s_p2.alg   = (lms_alg_t)iv;
                    p2_restart = true;
                }
            }
            break;
        case 'A':
            if (v >= 0.0f)
            {
                if (s_part == 1U)
                {
                    s_p1.amp = v;
                }
                else
                {
                    s_p2.noise_amp = v;
                    p2_restart     = true;
                }
            }
            break;
        default:
            break;
    }

    if (s_part == 1U)
    {
        switch (c)
        {
            case 'E':
                s_p1.input = (iv != 0) ? P1_IN_CONST : P1_IN_RANDOM;
                break;
            case 'V':
                s_p1.sigma_v = (v > 0.0f) ? v : 0.0f;
                break;
            case 'N':
                if ((iv >= (long)APP_P1_BLOCK) && (iv <= 1000000L))
                {
                    s_p1.n_total = (uint32_t)iv;
                }
                break;
            case 'R':
                s_p1.seed = (uint32_t)iv;
                break;
            case 'D':
                s_p1.send_diff = (iv != 0);
                break;
            case 'S':
                s_p1_start = true;
                break;
            case 'B':
                s_p1_sweep = true;
                break;
            default:
                break;
        }
    }
    else
    {
        switch (c)
        {
            case 'Q':
                s_p2.topo  = (iv != 0) ? P2_CORR_EN_REFERENCIA : P2_CORR_EN_RUIDO;
                p2_restart = true;
                break;
            case 'F':
                if ((iv >= (long)APP_FS_MIN) && (iv <= (long)APP_FS_MAX))
                {
                    s_p2_fs    = (uint32_t)iv;
                    p2_restart = true;
                }
                break;
            case 'O':
                if ((iv >= 0) && (iv < (long)P2_OUT_CNT))
                {
                    s_dac_sel = (p2_out_t)iv;
                }
                break;
            case 'Z':
                s_p2_reset_req = true;
                p2_print_header(uart_write, s_p2_fs_real); /* marca el reinicio en el registro */
                break;
            case 'C':
                p2_print_w(uart_write);
                break;
            case 'T':
                p2_print_timing();
                break;
            case 'M':
                s_p2_print       = (iv > 0);
                s_p2_print_every = (iv > 0) ? (uint32_t)iv : 1U;
                break;
            default:
                break;
        }
        if (p2_restart)
        {
            p2_start();
        }
    }
}

static void uart_poll_commands(void)
{
    while (LPUART_TransferGetRxRingBufferLength(APP_UART, &s_uart_handle) > 0U)
    {
        uint8_t ch;
        size_t got             = 0U;
        lpuart_transfer_t xfer = {.data = &ch, .dataSize = 1U};
        (void)LPUART_TransferReceiveNonBlocking(APP_UART, &s_uart_handle, &xfer, &got);
        if (got == 0U)
        {
            break;
        }
        if ((ch == '\n') || (ch == '\r'))
        {
            if (s_cmd_len > 0U)
            {
                s_cmd[s_cmd_len] = '\0';
                process_command(s_cmd);
                s_cmd_len = 0U;
            }
        }
        else if (s_cmd_len < (sizeof(s_cmd) - 1U))
        {
            s_cmd[s_cmd_len++] = (char)ch;
        }
        else
        {
            s_cmd_len = 0U;
        }
    }
}

static void handle_events(void)
{
    if (s_ev_sw3_long)
    {
        s_ev_sw3_long = false;
        set_part((s_part == 1U) ? 2U : 1U);
    }
    if (s_ev_sw2)
    {
        s_ev_sw2 = false;
        if (s_part == 1U)
        {
            s_p1_start = true;
        }
        else
        {
            s_p2_reset_req = true;
            p2_print_header(uart_write, s_p2_fs_real);
        }
    }
    if (s_ev_sw3_short)
    {
        s_ev_sw3_short = false;
        if (s_part == 1U)
        {
            s_p1_mu_idx = (s_p1_mu_idx + 1U) % P1_MU_CNT;
            s_p1.mu     = s_p1_mu_list[s_p1_mu_idx];
            print_help();
        }
        else
        {
            s_dac_sel = (p2_out_t)(((uint32_t)s_dac_sel + 1U) % (uint32_t)P2_OUT_CNT);
            print_help();
        }
    }
    uart_poll_commands();
}

/* Durante un experimento de la parte 1: SW2 o el comando X lo cortan */
static bool p1_abort(void)
{
    bool stop = false;
    if (s_ev_sw2)
    {
        s_ev_sw2 = false;
        stop     = true;
    }
    while (LPUART_TransferGetRxRingBufferLength(APP_UART, &s_uart_handle) > 0U)
    {
        uint8_t ch;
        size_t got             = 0U;
        lpuart_transfer_t xfer = {.data = &ch, .dataSize = 1U};
        (void)LPUART_TransferReceiveNonBlocking(APP_UART, &s_uart_handle, &xfer, &got);
        if ((got != 0U) && ((ch == 'X') || (ch == 'x')))
        {
            stop = true;
        }
    }
    GPIO_PortToggle(APP_LED_BLUE_GPIO, 1UL << APP_LED_BLUE_PIN);
    return stop;
}

static void p1_do_runs(void)
{
    const p1_io_t io = {uart_write, cycles_now, p1_abort};

    if (s_p1_sweep)
    {
        /* Barrido: misma entrada (misma semilla), cambia sólo mu */
        p1_cfg_t c = s_p1;
        uart_puts("P1B,INICIO\r\n");
        for (uint32_t i = 0U; i < P1_MU_CNT; i++)
        {
            c.mu = s_p1_mu_list[i];
            if (p1_run(&c, &io) == P1_CORTADO)
            {
                break;
            }
        }
        uart_puts("P1B,FIN\r\n");
    }
    else
    {
        (void)p1_run(&s_p1, &io);
    }
    s_p1_start = false;
    s_p1_sweep = false;
    GPIO_PortSet(APP_LED_BLUE_GPIO, 1UL << APP_LED_BLUE_PIN); /* apagado */
}

/*******************************************************************************
 * main
 ******************************************************************************/
int main(void)
{
    BOARD_InitBootClocks();
    APP_InitPins();

    gpio_init();
    uart_init();
    adc_init();
    dac_init();
    ctimer_init();
    cycle_counter_init();
    (void)SysTick_Config(SystemCoreClock / 1000U);

    p1_default_cfg(&s_p1);
    s_p1.n_total = APP_P1_N_DEFAULT;
    s_p1.block   = APP_P1_BLOCK;
    s_p1.mu      = s_p1_mu_list[s_p1_mu_idx];
    p2_default_cfg(&s_p2);
    s_p2_fs_real = (float)s_p2_fs;

    leds_update();
    print_help();

    for (;;)
    {
        handle_events();

        if (s_part == 1U)
        {
            if (s_p1_start || s_p1_sweep)
            {
                p1_do_runs();
            }
        }
        else
        {
            /* Bloques de estadísticas que dejó la ISR */
            while (s_stats_rd != s_stats_wr)
            {
                p2_stats_t st = s_stats_fifo[s_stats_rd % STATS_FIFO];
                s_stats_rd++;
                if (s_p2_print && (++s_p2_print_cnt >= s_p2_print_every))
                {
                    s_p2_print_cnt = 0U;
                    p2_print_stats(uart_write, &st);
                    GPIO_PortToggle(APP_LED_BLUE_GPIO, 1UL << APP_LED_BLUE_PIN);
                }
            }
        }
    }
}
