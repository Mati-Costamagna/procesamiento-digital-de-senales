/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * TP4: filtros adaptativos LMS en la FRDM-MCXN947.
 * NUCLEO 0 (cm33_core0): PROCESAMIENTO.
 *
 * El core0 hace todo el procesamiento. El core1 (proyecto TP4_core1) solo
 * atiende la UART: manda a la PC el texto que el core0 deja en la memoria
 * compartida y le pasa al core0 los comandos que llegan de la PC (MATLAB o
 * una terminal).
 *
 * El filtro adaptativo es arm_lms_q15 de CMSIS-DSP (punto fijo Q15) y tiene
 * N_COEF = 10 coeficientes.
 *
 * PARTE 1: identificacion de planta. La placa simula la planta (FIR fijo),
 *   la entrada (aleatoria o constante) y el filtro adaptativo. Cada 100
 *   muestras manda el MSE y la diferencia entre planta y filtro.
 *
 * PARTE 2: cancelacion de ruido. Igual que TP2/TP3, CTIMER0 MAT3 (8 kHz)
 *   dispara el ADC0 por hardware. En la ISR del ADC: s = senoidal del
 *   generador, se le suma ruido AWGN, el LMS lo cancela y salen por los DAC
 *   la salida limpia (DAC0) y la senal con ruido (DAC1).
 *
 * Pines, relojes y perifericos: ConfigTools (TP4_core0.mex -> board/).
 *
 * SW2: Parte 1 arranca un experimento | Parte 2 reinicia los coeficientes.
 * SW3: cambia de Parte 1 a Parte 2 y viceversa.
 * LED verde = Parte 1, LED rojo = Parte 2, LED azul = actividad
 * (fijo si el core1 no arranco).
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_ctimer.h"
#include "fsl_dac.h"
#include "fsl_gpio.h"
#include "fsl_lpadc.h"
#include "fsl_spc.h"
#include "mcmgr.h"
#include "arm_math.h"


/*******************************************************************************
 * Parametros
 ******************************************************************************/
#define N_COEF               10U    /* coeficientes de planta, correlador y filtro */
#define MUESTRAS_POR_BLOQUE  100U   /* el MSE se promedia cada 100 muestras */

/* Parte 1 */
#define P1_POST_SHIFT        0U     /* coeficientes en [-1, 1) */

/* Parte 2 */
#define P2_POST_SHIFT        1U     /* coeficientes en [-2, 2): C^-1 llega a 1,4 */
#define VOLTS_FONDO_ESCALA   1.65f  /* 1,0 en Q15 = 1,65 V */
#define VOLTS_POR_CUENTA_ADC (3.3f / 65536.0f)
#define CUENTAS_DAC_POR_VOLT (4096.0f / 3.3f)
#define DAC_MITAD            2048U
#define CAPTURA_MAX          4096U  /* muestras por captura (comando K / Z) */
#define DURACION_TRIG        8U     /* muestras en alto del pin TRIG */

#define ANTIRREBOTE_MS       250U
#define ESPERA_CORE1_MS      1000U

#define LED_ENCENDIDO        0U     /* LEDs activos en bajo */
#define LED_APAGADO          1U


/*******************************************************************************
 * Memoria compartida con el core1.
 * La misma estructura esta copiada en TP4_core1.c: tienen que ser identicas.
 *
 * tx: texto del core0 para la UART. cmd: lineas de comando que llegan.
 * Cada contador lo escribe un solo nucleo y crece sin limite; la posicion en
 * el buffer es contador % tamano. Como hay un solo productor y un solo
 * consumidor por cola no hacen falta semaforos: alcanza con __DMB() antes de
 * publicar el contador.
 ******************************************************************************/
#define DIRECCION_COMPARTIDA 0x2004C000UL  /* 8 KB fuera de la RAM de los dos nucleos */
#define DIRECCION_CORE1      0x2004E000UL  /* donde el IDE deja la imagen del core1 */
#define VALOR_MAGICO         0x54503443UL
#define TX_TAMANO            4096U
#define CMD_CANTIDAD         8U
#define CMD_LARGO            32U

typedef struct
{
    volatile uint32_t magico;        /* el core0 lo escribe cuando termina de inicializar */
    volatile uint32_t core1_listo;   /* el core1 lo pone en 1 cuando la UART esta lista */
    volatile uint32_t tx_escritos;   /* bytes escritos por el core0 */
    volatile uint32_t tx_leidos;     /* bytes mandados por el core1 */
    char tx[TX_TAMANO];
    volatile uint32_t cmd_escritos;  /* lineas escritas por el core1 */
    volatile uint32_t cmd_leidos;    /* lineas ejecutadas por el core0 */
    char cmd[CMD_CANTIDAD][CMD_LARGO];
    volatile uint32_t cortar;        /* el core1 lo pone en 1 al recibir una X */
} memoria_compartida_t;

static memoria_compartida_t *const compartida = (memoria_compartida_t *)DIRECCION_COMPARTIDA;


/*******************************************************************************
 * Coeficientes fijos
 ******************************************************************************/

/* Planta de la Parte 1: p[k] = 0,32 e^(-k/3) sin(0,3 pi (k+1)) + 0,02 cos(0,9 k).
 * Suma de |p[k]| = 0,896 < 1, asi la salida cabe en Q15 para |x| <= 1. */
static const float planta[N_COEF] = {
    0.2789f, 0.2305f, 0.0462f, -0.0873f, -0.1023f, -0.0397f, 0.0261f, 0.0495f, 0.0302f, -0.0049f,
};

/* Correlador de la Parte 2: c[k] = sqrt(1 - 0,7^2) 0,7^k. Energia ~1 y fase
 * minima (ceros en |z| = 0,7): su inversa es estable y entra en 10 coeficientes. */
static const float correlador[N_COEF] = {
    0.7141f, 0.4999f, 0.3499f, 0.2450f, 0.1715f, 0.1200f, 0.0840f, 0.0588f, 0.0412f, 0.0288f,
};

/* Filtro optimo de Wiener de cada topologia de la Parte 2 (R w = p).
 *  - Correlador en el camino del ruido: W = C.
 *  - Correlador en la referencia (figura 2): W = C^-1 recortado a 10
 *    coeficientes, calculado en MATLAB con w = toeplitz(r) \ [c(1); zeros(9,1)],
 *    donde r(m) = sum c(k) c(k+m). */
static const float w_optimo_figura2[N_COEF] = {
    1.39953f, -0.97954f, 0.00010f, -0.00018f, 0.00010f, 0.00010f, -0.00007f, 0.00000f, -0.00008f, 0.01420f,
};

/* Valores de mu del barrido de la Parte 1 (comando B). Con entrada aleatoria
 * de amplitud 1 el limite es 2 / (N_COEF Px) = 0,6. */
static const float mu_barrido[] = {0.002f, 0.01f, 0.05f, 0.2f, 0.5f, 0.7f};
#define CANT_MU_BARRIDO (sizeof(mu_barrido) / sizeof(mu_barrido[0]))


/*******************************************************************************
 * Variables
 ******************************************************************************/
static volatile uint32_t parte_activa = 1U;   /* 1 o 2 */
static volatile uint32_t ms;                  /* base de tiempo de 1 ms (SysTick) */
static volatile bool sw2_apretado;
static volatile bool sw3_apretado;
static uint32_t sw2_ultimo_ms;
static uint32_t sw3_ultimo_ms;
static bool core1_funciona;

/* Configuracion de la Parte 1 (la cambian los comandos) */
static float p1_mu                = 0.01f;
static bool p1_entrada_constante  = false;
static float p1_amplitud          = 1.0f;    /* 0 < A <= 1 */
static float p1_ruido_medicion    = 0.0f;    /* desvio del ruido sumado a d(n) */
static uint32_t p1_cant_muestras  = 5000U;
static uint32_t p1_semilla        = 1U;
static bool p1_enviar_diferencias = true;
static bool p1_pedido_correr;
static bool p1_pedido_barrido;

/* Configuracion de la Parte 2 */
static float p2_mu                   = 0.02f;
static float p2_ruido_sigma          = 0.25f;  /* desvio del AWGN, en volts */
static bool p2_correlador_referencia = true;   /* true = figura 2 de la consigna */
static uint32_t p2_bloques_a_enviar  = 1U;     /* manda 1 de cada k bloques (0 = ninguno) */
static float fs_hz;

/* Estado de la Parte 2 (lo usa la ISR del ADC) */
enum
{
    SENAL_D = 0,   /* senal + ruido */
    SENAL_E,       /* salida del cancelador */
    SENAL_Y,       /* ruido estimado */
    SENAL_S,       /* senal del generador sola */
    SENAL_RESIDUO, /* (e - s) x 4 */
    SENAL_X,       /* referencia del filtro adaptativo */
    CANT_SENALES
};

static volatile bool p2_corriendo;
static volatile bool p2_pedido_reinicio;
static volatile uint32_t dac0_senal = SENAL_E;
static volatile uint32_t dac1_senal = SENAL_D;
static volatile float continua_volts = 1.65f;
static float correlador_historia[N_COEF];
static uint32_t ruido_semilla = 1U;
static uint32_t trig_restante;
static uint32_t muestra_numero;

/* Estadisticas de un bloque de 100 muestras (ISR -> lazo principal) */
typedef struct
{
    uint32_t n;
    float potencia_ruido;    /* del ruido que se le sumo a la senal */
    float potencia_residuo;  /* de e - s: el ruido que no se cancelo */
    float potencia_senal;
    float error_coef;        /* ||w_optimo - w||^2 */
} bloque_t;

static float suma_ruido;
static float suma_residuo;
static float suma_senal;
static uint32_t muestras_en_bloque;
static bloque_t bloque_terminado;
static volatile bool hay_bloque_nuevo;
static volatile uint32_t bloques_perdidos;

/* Tiempo de procesamiento de cada muestra (ciclos de reloj) */
static volatile uint64_t ciclos_suma;
static volatile uint32_t ciclos_max;
static volatile uint32_t ciclos_cantidad;

/* Captura de formas de onda para MATLAB */
enum
{
    CAPTURA_LIBRE = 0,
    CAPTURA_ESPERA_REINICIO,   /* arranca con el proximo reinicio de los coeficientes */
    CAPTURA_GRABANDO,          /* la ISR guarda muestras */
    CAPTURA_LISTA,             /* llena: falta mandar el encabezado */
    CAPTURA_ENVIANDO,          /* el lazo principal la manda de a una linea */
};
static volatile uint32_t captura_estado = CAPTURA_LIBRE;
static volatile uint32_t captura_largo  = 1000U;
static volatile uint32_t captura_grabadas;
static uint32_t captura_enviadas;
static float captura[5][CAPTURA_MAX];   /* d, e, y, s, x */

/* Filtro adaptativo: arm_lms_q15 de CMSIS-DSP */
static arm_lms_instance_q15 lms;
static q15_t lms_coef[N_COEF];     /* orden CMSIS: lms_coef[0] multiplica la muestra mas vieja */
static q15_t lms_estado[N_COEF];   /* N_COEF + blockSize - 1, con blockSize = 1 */

static char linea[1024];            /* buffer para armar las lineas de texto */


/*******************************************************************************
 * Comunicacion: cola de texto hacia el core1
 ******************************************************************************/

/* Deja una linea en la cola. Si esperar = true y no hay lugar, espera a que
 * el core1 la vacie (hasta 2 s); si esperar = false la descarta. */
static void poner_en_cola(const char *texto, bool esperar)
{
    const uint32_t largo = strlen(texto);
    const uint32_t inicio = ms;

    if (!compartida->core1_listo)
    {
        return;
    }
    while ((TX_TAMANO - (compartida->tx_escritos - compartida->tx_leidos)) < largo)
    {
        if (!esperar || ((ms - inicio) > 2000U))
        {
            return;
        }
    }

    const uint32_t posicion = compartida->tx_escritos;
    for (uint32_t i = 0U; i < largo; i++)
    {
        compartida->tx[(posicion + i) % TX_TAMANO] = texto[i];
    }
    __DMB();   /* los datos tienen que estar escritos antes de avisar */
    compartida->tx_escritos = posicion + largo;
}

/* printf hacia la UART (a traves del core1) */
static void enviar(const char *formato, ...)
{
    va_list args;
    va_start(args, formato);
    vsnprintf(linea, sizeof(linea), formato, args);
    va_end(args);
    poner_en_cola(linea, true);
}

/* "prefijo,v0,v1,...,v9" */
static void enviar_vector(const char *prefijo, const float v[N_COEF])
{
    int largo = snprintf(linea, sizeof(linea), "%s", prefijo);
    for (uint32_t k = 0U; k < N_COEF; k++)
    {
        largo += snprintf(&linea[largo], sizeof(linea) - largo, ",%.4e", v[k]);
    }
    snprintf(&linea[largo], sizeof(linea) - largo, "\r\n");
    poner_en_cola(linea, true);
}

/* Saca un comando de la cola del core1. Devuelve false si no hay. */
static bool leer_comando(char comando[CMD_LARGO])
{
    const uint32_t leidos = compartida->cmd_leidos;
    if (leidos == compartida->cmd_escritos)
    {
        return false;
    }
    __DMB();
    memcpy(comando, compartida->cmd[leidos % CMD_CANTIDAD], CMD_LARGO);
    comando[CMD_LARGO - 1U] = '\0';
    __DMB();
    compartida->cmd_leidos = leidos + 1U;
    return true;
}


/*******************************************************************************
 * Numeros aleatorios: xorshift32 (uniforme) y metodo polar de Marsaglia (AWGN)
 ******************************************************************************/
static float aleatorio_uniforme(uint32_t *estado)   /* uniforme en [-1, 1) */
{
    uint32_t x = *estado;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *estado = x;
    return (float)(int32_t)((x >> 8) - 0x800000U) / 8388608.0f;
}

/* ln(x) para x > 0, sin math.h: x = m 2^e con m en [1, 2), y
 * ln(m) = 2 atanh((m - 1) / (m + 1)) con 5 terminos de la serie. */
static float logaritmo_natural(float x)
{
    union
    {
        float f;
        uint32_t u;
    } bits = {.f = x};

    const int exponente = (int)((bits.u >> 23) & 0xFFU) - 127;
    bits.u = (bits.u & 0x007FFFFFU) | 0x3F800000U;
    const float t  = (bits.f - 1.0f) / (bits.f + 1.0f);
    const float t2 = t * t;
    const float ln_mantisa = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f + t2 / 9.0f))));
    return 0.69314718f * (float)exponente + ln_mantisa;
}

/* Normal N(0, 1) por el metodo polar de Marsaglia */
static float aleatorio_gaussiano(uint32_t *estado)
{
    float u;
    float v;
    float s;
    do
    {
        u = aleatorio_uniforme(estado);
        v = aleatorio_uniforme(estado);
        s = u * u + v * v;
    } while ((s >= 1.0f) || (s == 0.0f));

    float factor;
    arm_sqrt_f32(-2.0f * logaritmo_natural(s) / s, &factor);
    return u * factor;
}


/*******************************************************************************
 * FIR fijo (planta y correlador): y(n) = sum h[k] x(n-k), historia[0] = x(n)
 ******************************************************************************/
static float fir_fijo(const float h[N_COEF], float historia[N_COEF], float x)
{
    for (uint32_t k = N_COEF - 1U; k > 0U; k--)
    {
        historia[k] = historia[k - 1U];
    }
    historia[0] = x;

    float y = 0.0f;
    for (uint32_t k = 0U; k < N_COEF; k++)
    {
        y += h[k] * historia[k];
    }
    return y;
}


/*******************************************************************************
 * Filtro adaptativo: arm_lms_q15
 *
 * En cada muestra arm_lms_q15 hace:
 *   y = (sum coef * x) >> (15 - postShift)     coeficiente real = coef * 2^postShift
 *   e = d - y                                   en 16 bits, SIN saturar
 *   coef += ((e * mu) >> 15) * x >> 15          trunca hacia -infinito
 * El paso efectivo es mu * 2^postShift. Las senales tienen que quedar dentro
 * de [-1, 1): si |d - y| pasa de 1, e "da la vuelta".
 ******************************************************************************/
static q15_t a_q15(float valor)   /* con redondeo y saturacion */
{
    float t = valor * 32768.0f;
    t += (t >= 0.0f) ? 0.5f : -0.5f;
    if (t > 32767.0f)
    {
        return 32767;
    }
    if (t < -32768.0f)
    {
        return -32768;
    }
    return (q15_t)t;
}

static float de_q15(q15_t valor)
{
    return (float)valor / 32768.0f;
}

static void lms_iniciar(float mu, uint32_t post_shift)
{
    memset(lms_coef, 0, sizeof(lms_coef));
    arm_lms_init_q15(&lms, N_COEF, lms_coef, lms_estado, a_q15(mu), 1U, post_shift);
}

/* Una muestra. x y d en unidades de Q15 (-1..1). Devuelve y; el error en *e. */
static float lms_procesar(float x, float d, float *e)
{
    q15_t x_q15 = a_q15(x);
    q15_t d_q15 = a_q15(d);
    q15_t y_q15;
    q15_t e_q15;
    arm_lms_q15(&lms, &x_q15, &d_q15, &y_q15, &e_q15, 1U);
    *e = de_q15(e_q15);
    return de_q15(y_q15);
}

/* Coeficientes en valor real y en orden natural (w[0] multiplica a x(n)) */
static void lms_leer_coeficientes(float w[N_COEF])
{
    const float escala = (float)(1U << lms.postShift) / 32768.0f;
    for (uint32_t k = 0U; k < N_COEF; k++)
    {
        w[k] = (float)lms_coef[N_COEF - 1U - k] * escala;
    }
}

/* En Q15 la divergencia se ve como coeficientes clavados en la saturacion */
static bool lms_saturado(void)
{
    for (uint32_t k = 0U; k < N_COEF; k++)
    {
        if ((lms_coef[k] >= 32700) || (lms_coef[k] <= -32700))
        {
            return true;
        }
    }
    return false;
}


/*******************************************************************************
 * LEDs y salidas de medicion
 ******************************************************************************/
static void actualizar_leds(void)
{
    GPIO_PinWrite(BOARD_INITLEDSPINS_LED_GREEN_GPIO, BOARD_INITLEDSPINS_LED_GREEN_GPIO_PIN,
                  (parte_activa == 1U) ? LED_ENCENDIDO : LED_APAGADO);
    GPIO_PinWrite(BOARD_INITLEDSPINS_LED_RED_GPIO, BOARD_INITLEDSPINS_LED_RED_GPIO_PIN,
                  (parte_activa == 2U) ? LED_ENCENDIDO : LED_APAGADO);
    GPIO_PinWrite(BOARD_INITLEDSPINS_LED_BLUE_GPIO, BOARD_INITLEDSPINS_LED_BLUE_GPIO_PIN,
                  core1_funciona ? LED_APAGADO : LED_ENCENDIDO);
}

static void parpadear_led_azul(void)
{
    if (core1_funciona)
    {
        GPIO_PortToggle(BOARD_INITLEDSPINS_LED_BLUE_GPIO, BOARD_INITLEDSPINS_LED_BLUE_GPIO_PIN_MASK);
    }
}

static uint32_t volts_a_dac(float volts)   /* volts centrados en 0 -> DAC centrado en 1,65 V */
{
    float cuenta = (float)DAC_MITAD + volts * CUENTAS_DAC_POR_VOLT;
    if (cuenta < 0.0f)
    {
        cuenta = 0.0f;
    }
    if (cuenta > 4095.0f)
    {
        cuenta = 4095.0f;
    }
    return (uint32_t)cuenta;
}


/*******************************************************************************
 * PARTE 1: identificacion de planta
 ******************************************************************************/
static float p1_potencia_entrada(void)
{
    const float a2 = p1_amplitud * p1_amplitud;
    return p1_entrada_constante ? a2 : (a2 / 3.0f);   /* uniforme en [-A, A): A^2 / 3 */
}

/* Hace un experimento completo con el mu dado. Devuelve false si se corto. */
static bool p1_correr(float mu)
{
    float entrada_historia[N_COEF] = {0};
    float w[N_COEF];
    float diferencia[N_COEF];
    uint32_t semilla_entrada = p1_semilla;
    uint32_t semilla_ruido   = p1_semilla ^ 0xA5A5A5A5U;
    uint32_t ciclos_lms      = 0U;
    uint32_t n               = 0U;
    float mse                = 0.0f;
    float error_coef         = 0.0f;
    const char *estado       = "OK";

    /* Teoria (convencion de CMSIS, mu_ef = 2^postShift mu):
     *   mu_max = 2 / (N_COEF Px),   tau = 1 / (2 mu lambda) muestras
     *   entrada aleatoria: lambda = Px | constante: lambda = N_COEF A^2 */
    const float px     = p1_potencia_entrada();
    const float lambda = p1_entrada_constante ? ((float)N_COEF * px) : px;
    const float mu_max = 2.0f / ((float)N_COEF * px * (float)(1U << P1_POST_SHIFT));
    const float tau    = 1.0f / (2.0f * mu * (float)(1U << P1_POST_SHIFT) * lambda);

    lms_iniciar(mu, P1_POST_SHIFT);
    compartida->cortar = 0U;

    enviar("P1I,alg=CMSIS_LMS_Q15,mu=%.4e,ps=%u,in=%s,A=%.4e,Px=%.4e,L=%u,N=%lu,blk=%u,sv=%.4e,"
           "seed=%lu,mu_max=%.4e,tau=%.4e\r\n",
           de_q15(lms.mu), P1_POST_SHIFT, p1_entrada_constante ? "CONST" : "RANDOM", p1_amplitud, px,
           N_COEF, (unsigned long)p1_cant_muestras, MUESTRAS_POR_BLOQUE, p1_ruido_medicion,
           (unsigned long)p1_semilla, mu_max, tau);
    enviar_vector("P1P", planta);

    while (n < p1_cant_muestras)
    {
        float suma_error2 = 0.0f;
        float suma_d2     = 0.0f;
        uint32_t m;

        for (m = 0U; (m < MUESTRAS_POR_BLOQUE) && (n < p1_cant_muestras); m++, n++)
        {
            /* entrada, cuantizada a Q15: la planta ve la misma muestra que el filtro */
            float x = p1_entrada_constante ? p1_amplitud : p1_amplitud * aleatorio_uniforme(&semilla_entrada);
            x = de_q15(a_q15(x));

            /* planta (+ ruido de medicion, uniforme de desvio sigma) */
            float d = fir_fijo(planta, entrada_historia, x);
            d += p1_ruido_medicion * 1.7320508f * aleatorio_uniforme(&semilla_ruido);

            /* filtro adaptativo */
            float e;
            const uint32_t inicio = DWT->CYCCNT;
            (void)lms_procesar(x, d, &e);
            ciclos_lms += DWT->CYCCNT - inicio;

            suma_error2 += e * e;
            suma_d2 += d * d;
        }

        /* cada 100 muestras: MSE del bloque y error de los coeficientes */
        mse = suma_error2 / (float)m;
        lms_leer_coeficientes(w);
        error_coef = 0.0f;
        for (uint32_t k = 0U; k < N_COEF; k++)
        {
            diferencia[k] = planta[k] - w[k];
            error_coef += diferencia[k] * diferencia[k];
        }

        enviar("P1M,%lu,%.4e,%.4e\r\n", (unsigned long)n, mse, error_coef);
        if (p1_enviar_diferencias)
        {
            char prefijo[16];
            snprintf(prefijo, sizeof(prefijo), "P1D,%lu", (unsigned long)n);
            enviar_vector(prefijo, diferencia);
        }
        parpadear_led_azul();

        if (lms_saturado() || (mse > 4.0f * suma_d2 / (float)m))
        {
            estado = "DIVERGE";
            break;
        }
        if (sw2_apretado || compartida->cortar)
        {
            sw2_apretado = false;
            estado       = "CORTADO";
            break;
        }
    }

    lms_leer_coeficientes(w);
    enviar_vector("P1W", w);
    enviar("P1F,%lu,%.4e,%.4e,%lu,%s\r\n", (unsigned long)n, mse, error_coef,
           (unsigned long)((n > 0U) ? (ciclos_lms / n) : 0U), estado);
    actualizar_leds();
    return strcmp(estado, "CORTADO") != 0;
}


/*******************************************************************************
 * PARTE 2: cancelacion de ruido
 ******************************************************************************/
static void p2_enviar_encabezado(void)
{
    /* Potencia de la referencia en unidades de Q15 (la energia de c es ~1) */
    const float sigma = p2_ruido_sigma / VOLTS_FONDO_ESCALA;
    const float px    = sigma * sigma;
    const float mu_ef = de_q15(lms.mu) * (float)(1U << P2_POST_SHIFT);

    enviar("P2I,alg=CMSIS_LMS_Q15,mu=%.4e,ps=%u,vfs=%.3f,ruido=%.4e,topo=%s,fs=%.1f,blk=%u,L=%u,"
           "mu_max=%.4e,tau=%.4e\r\n",
           de_q15(lms.mu), P2_POST_SHIFT, VOLTS_FONDO_ESCALA, p2_ruido_sigma,
           p2_correlador_referencia ? "REFERENCIA" : "RUIDO", fs_hz, MUESTRAS_POR_BLOQUE, N_COEF,
           2.0f / ((float)N_COEF * px * (float)(1U << P2_POST_SHIFT)), 1.0f / (2.0f * mu_ef * px));
}

static void p2_arrancar(void)
{
    p2_corriendo = false;
    CTIMER_StopTimer(CTIMER0_PERIPHERAL);

    lms_iniciar(p2_mu, P2_POST_SHIFT);
    memset(correlador_historia, 0, sizeof(correlador_historia));
    ruido_semilla      = 1U;
    muestras_en_bloque = 0U;
    suma_ruido         = 0.0f;
    suma_residuo       = 0.0f;
    suma_senal         = 0.0f;
    hay_bloque_nuevo   = false;
    captura_estado     = CAPTURA_LIBRE;
    ciclos_suma        = 0U;
    ciclos_max         = 0U;
    ciclos_cantidad    = 0U;

    p2_enviar_encabezado();
    p2_corriendo = true;
    CTIMER_StartTimer(CTIMER0_PERIPHERAL);   /* MAT3 conmuta a 16 kHz: cuadrada de 8 kHz -> trigger del ADC0 */
}

static void p2_detener(void)
{
    p2_corriendo = false;
    CTIMER_StopTimer(CTIMER0_PERIPHERAL);
    DAC_SetData(DAC0_PERIPHERAL, DAC_MITAD);
    DAC_SetData(DAC1_PERIPHERAL, DAC_MITAD);
    GPIO_PortClear(BOARD_INITTESTPINS_TRIG_GPIO, BOARD_INITTESTPINS_TRIG_GPIO_PIN_MASK);
}

/* Reinicia los coeficientes (Z o SW2) y, si n > 0, captura n muestras desde ese momento */
static void p2_reiniciar(uint32_t muestras_a_capturar)
{
    if ((muestras_a_capturar > 0U) && (captura_estado == CAPTURA_LIBRE))
    {
        captura_largo  = (muestras_a_capturar > CAPTURA_MAX) ? CAPTURA_MAX : muestras_a_capturar;
        captura_estado = CAPTURA_ESPERA_REINICIO;
    }

    /* el reinicio lo hace la ISR en la proxima muestra */
    p2_pedido_reinicio = true;
    const uint32_t inicio = ms;
    while (p2_pedido_reinicio && p2_corriendo && ((ms - inicio) < 10U))
    {
    }
    hay_bloque_nuevo = false;   /* descarta el bloque de antes del reinicio */
    p2_enviar_encabezado();
}

static void p2_capturar(uint32_t muestras)
{
    if (captura_estado == CAPTURA_LIBRE)
    {
        captura_largo    = (muestras == 0U) ? 1000U : ((muestras > CAPTURA_MAX) ? CAPTURA_MAX : muestras);
        captura_grabadas = 0U;
        captura_estado   = CAPTURA_GRABANDO;
    }
}

/* Procesa una muestra del ADC (se llama desde la ISR) */
static void p2_procesar_muestra(uint16_t cuenta_adc)
{
    GPIO_PortSet(BOARD_INITTESTPINS_PROC_GPIO, BOARD_INITTESTPINS_PROC_GPIO_PIN_MASK);
    const uint32_t inicio = DWT->CYCCNT;

    if (p2_pedido_reinicio)
    {
        p2_pedido_reinicio = false;
        lms_iniciar(p2_mu, P2_POST_SHIFT);
        trig_restante = DURACION_TRIG;
        GPIO_PortSet(BOARD_INITTESTPINS_TRIG_GPIO, BOARD_INITTESTPINS_TRIG_GPIO_PIN_MASK);
        if (captura_estado == CAPTURA_ESPERA_REINICIO)
        {
            captura_grabadas = 0U;
            captura_estado   = CAPTURA_GRABANDO;
        }
    }

    /* senal del generador sin la continua (seguimiento lento de la continua) */
    const float volts = (float)cuenta_adc * VOLTS_POR_CUENTA_ADC;
    continua_volts += (volts - continua_volts) / 4096.0f;
    const float s = volts - continua_volts;

    /* ruido AWGN y su version filtrada por el correlador */
    const float r          = p2_ruido_sigma * aleatorio_gaussiano(&ruido_semilla);
    const float r_filtrado = fir_fijo(correlador, correlador_historia, r);

    /* figura 2: d = s + r y la referencia es C{r} | clasico: d = s + C{r} y la referencia es r */
    const float ruido      = p2_correlador_referencia ? r : r_filtrado;
    const float referencia = p2_correlador_referencia ? r_filtrado : r;
    const float d          = s + ruido;

    /* cancelador: arm_lms_q15, con 1,0 en Q15 = 1,65 V */
    float e;
    float y = lms_procesar(referencia / VOLTS_FONDO_ESCALA, d / VOLTS_FONDO_ESCALA, &e);
    e *= VOLTS_FONDO_ESCALA;
    y *= VOLTS_FONDO_ESCALA;

    const float senales[CANT_SENALES] = {d, e, y, s, 4.0f * (e - s), referencia};
    DAC_SetData(DAC0_PERIPHERAL, volts_a_dac(senales[dac0_senal]));
    DAC_SetData(DAC1_PERIPHERAL, volts_a_dac(senales[dac1_senal]));

    /* captura para MATLAB */
    if (captura_estado == CAPTURA_GRABANDO)
    {
        const uint32_t i = captura_grabadas;
        captura[0][i] = d;
        captura[1][i] = e;
        captura[2][i] = y;
        captura[3][i] = s;
        captura[4][i] = referencia;
        captura_grabadas = i + 1U;
        if (captura_grabadas >= captura_largo)
        {
            __DMB();
            captura_estado = CAPTURA_LISTA;
        }
    }

    /* estadisticas por bloque de 100 muestras */
    muestra_numero++;
    suma_ruido += ruido * ruido;
    suma_residuo += (e - s) * (e - s);
    suma_senal += s * s;
    if (++muestras_en_bloque == MUESTRAS_POR_BLOQUE)
    {
        if (!hay_bloque_nuevo)
        {
            float w[N_COEF];
            lms_leer_coeficientes(w);
            float error_coef = 0.0f;
            for (uint32_t k = 0U; k < N_COEF; k++)
            {
                const float optimo = p2_correlador_referencia ? w_optimo_figura2[k] : correlador[k];
                error_coef += (optimo - w[k]) * (optimo - w[k]);
            }
            bloque_terminado.n                = muestra_numero;
            bloque_terminado.potencia_ruido   = suma_ruido / MUESTRAS_POR_BLOQUE;
            bloque_terminado.potencia_residuo = suma_residuo / MUESTRAS_POR_BLOQUE;
            bloque_terminado.potencia_senal   = suma_senal / MUESTRAS_POR_BLOQUE;
            bloque_terminado.error_coef       = error_coef;
            __DMB();   /* el bloque completo antes de levantar la bandera */
            hay_bloque_nuevo                  = true;
        }
        else
        {
            bloques_perdidos++;
        }
        muestras_en_bloque = 0U;
        suma_ruido         = 0.0f;
        suma_residuo       = 0.0f;
        suma_senal         = 0.0f;
    }

    if ((trig_restante > 0U) && (--trig_restante == 0U))
    {
        GPIO_PortClear(BOARD_INITTESTPINS_TRIG_GPIO, BOARD_INITTESTPINS_TRIG_GPIO_PIN_MASK);
    }

    const uint32_t ciclos = DWT->CYCCNT - inicio;
    ciclos_suma += ciclos;
    ciclos_cantidad++;
    if (ciclos > ciclos_max)
    {
        ciclos_max = ciclos;
    }
    GPIO_PortClear(BOARD_INITTESTPINS_PROC_GPIO, BOARD_INITTESTPINS_PROC_GPIO_PIN_MASK);
}

/* Lazo principal de la Parte 2: estadisticas y captura, sin bloquear */
static void p2_atender(void)
{
    static uint32_t contador_bloques;

    if (hay_bloque_nuevo)
    {
        __DMB();
        const bloque_t b = bloque_terminado;
        hay_bloque_nuevo = false;

        /* mientras se manda una captura no se mandan estadisticas */
        if ((p2_bloques_a_enviar > 0U) && (captura_estado < CAPTURA_LISTA) &&
            (++contador_bloques >= p2_bloques_a_enviar))
        {
            contador_bloques = 0U;
            snprintf(linea, sizeof(linea), "P2M,%lu,%.4e,%.4e,%.4e,%.4e\r\n", (unsigned long)b.n, b.potencia_ruido,
                     b.potencia_residuo, b.potencia_senal, b.error_coef);
            poner_en_cola(linea, false);
            parpadear_led_azul();
        }
    }

    /* captura terminada: encabezado y despues de a una linea, si hay lugar en la cola */
    if (captura_estado == CAPTURA_LISTA)
    {
        enviar("CAPI,n=%lu,fs=%.1f,mu=%.4e,ruido=%.4e,topo=%s,senales=d;e;y;s;x\r\n", (unsigned long)captura_largo,
               fs_hz, de_q15(lms.mu), p2_ruido_sigma, p2_correlador_referencia ? "REFERENCIA" : "RUIDO");
        captura_enviadas = 0U;
        captura_estado   = CAPTURA_ENVIANDO;
    }
    if (captura_estado == CAPTURA_ENVIANDO)
    {
        while (captura_enviadas < captura_largo)
        {
            const uint32_t i = captura_enviadas;
            snprintf(linea, sizeof(linea), "CAP,%lu,%.4e,%.4e,%.4e,%.4e,%.4e\r\n", (unsigned long)i, captura[0][i], captura[1][i],
                     captura[2][i], captura[3][i], captura[4][i]);
            if ((TX_TAMANO - (compartida->tx_escritos - compartida->tx_leidos)) < strlen(linea))
            {
                return;   /* la cola esta llena: se sigue en la proxima vuelta */
            }
            poner_en_cola(linea, false);
            captura_enviadas++;
        }
        enviar("CAPF,n=%lu\r\n", (unsigned long)captura_largo);
        captura_estado = CAPTURA_LIBRE;
    }
}

static void p2_enviar_tiempos(void)
{
    DisableIRQ(ADC0_IRQN);
    const uint64_t suma     = ciclos_suma;
    const uint32_t cantidad = ciclos_cantidad;
    const uint32_t maximo   = ciclos_max;
    ciclos_suma     = 0U;
    ciclos_cantidad = 0U;
    ciclos_max      = 0U;
    EnableIRQ(ADC0_IRQN);

    const uint32_t promedio = (cantidad > 0U) ? (uint32_t)(suma / cantidad) : 0U;
    enviar("P2T,cyc_prom=%lu,cyc_max=%lu,n=%lu,f_cpu=%lu,us_prom=%.3f,us_disp=%.3f,bloques_perdidos=%lu\r\n",
           (unsigned long)promedio, (unsigned long)maximo, (unsigned long)cantidad, (unsigned long)SystemCoreClock,
           1.0e6f * promedio / SystemCoreClock, 1.0e6f / fs_hz, (unsigned long)bloques_perdidos);
}

static void p2_enviar_coeficientes(void)
{
    float w[N_COEF];
    lms_leer_coeficientes(w);
    enviar_vector("P2W", w);
    enviar_vector("P2O", p2_correlador_referencia ? w_optimo_figura2 : correlador);
    enviar_vector("P2C", correlador);
}


/*******************************************************************************
 * Interrupciones
 ******************************************************************************/
void SysTick_Handler(void)
{
    ms++;
}

/* SW3 (P0_6) -> GPIO0 interrupcion 0 */
void GPIO0_INT_0_IRQHANDLER(void)
{
    const uint32_t banderas = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);
    if ((ms - sw3_ultimo_ms) >= ANTIRREBOTE_MS)
    {
        sw3_ultimo_ms = ms;
        sw3_apretado  = true;
    }
    GPIO_GpioClearInterruptChannelFlags(GPIO0, banderas, 0U);
    SDK_ISR_EXIT_BARRIER;
}

/* SW2 (P0_23) -> GPIO0 interrupcion 1 */
void GPIO0_INT_1_IRQHANDLER(void)
{
    const uint32_t banderas = GPIO_GpioGetInterruptChannelFlags(GPIO0, 1U);
    if ((ms - sw2_ultimo_ms) >= ANTIRREBOTE_MS)
    {
        sw2_ultimo_ms = ms;
        sw2_apretado  = true;
    }
    GPIO_GpioClearInterruptChannelFlags(GPIO0, banderas, 1U);
    SDK_ISR_EXIT_BARRIER;
}

/* ADC0: una conversion por cada flanco de CTIMER0 MAT3 */
void ADC0_IRQHANDLER(void)
{
    LPADC_ClearTriggerStatusFlags(ADC0_PERIPHERAL, LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL));
    LPADC_ClearStatusFlags(ADC0_PERIPHERAL, LPADC_GetStatusFlags(ADC0_PERIPHERAL));

    lpadc_conv_result_t resultado;
    while (LPADC_GetConvResult(ADC0_PERIPHERAL, &resultado, 0U))
    {
        if (p2_corriendo)
        {
            p2_procesar_muestra(resultado.convValue);
        }
    }
    SDK_ISR_EXIT_BARRIER;
}


/*******************************************************************************
 * Comandos (llegan de la PC a traves del core1)
 ******************************************************************************/
static void enviar_ayuda(void)
{
    enviar("\r\n# TP4 PDS - LMS (arm_lms_q15) - FRDM-MCXN947 (core0: proceso, core1: UART)\r\n"
           "# 1 / 2: Parte 1 / Parte 2 (o SW3) | U<mu>: paso, 0 < mu < 1\r\n"
           "# P1: A<amp> (0..1), E<0|1> aleatoria/constante, V<sigma> ruido de medicion,\r\n"
           "#     N<muestras>, R<semilla>, D<0|1> diferencias, S correr (o SW2), B barrido, X cortar\r\n"
           "# P2: A<sigma> ruido (V), Q<0|1> correlador en el ruido/referencia (figura 2),\r\n"
           "#     O<n> DAC0, P<n> DAC1 (0 d, 1 e, 2 y, 3 s, 4 residuo, 5 referencia),\r\n"
           "#     Z[n] reiniciar (o SW2) y capturar n, K<n> capturar, C coeficientes,\r\n"
           "#     T tiempos, M<k> mandar 1 de cada k bloques (0 = no)\r\n");
    if (parte_activa == 1U)
    {
        enviar("#CFG,parte=1,mu=%.4e,in=%s,A=%.4e,sv=%.4e,N=%lu,seed=%lu\r\n", p1_mu,
               p1_entrada_constante ? "CONST" : "RANDOM", p1_amplitud, p1_ruido_medicion,
               (unsigned long)p1_cant_muestras, (unsigned long)p1_semilla);
    }
    else
    {
        enviar("#CFG,parte=2,mu=%.4e,ruido=%.4e,topo=%s,fs=%.1f,dac0=%lu,dac1=%lu\r\n", p2_mu, p2_ruido_sigma,
               p2_correlador_referencia ? "REFERENCIA" : "RUIDO", fs_hz, (unsigned long)dac0_senal,
               (unsigned long)dac1_senal);
    }
}

static void cambiar_parte(uint32_t parte)
{
    if (parte != parte_activa)
    {
        parte_activa = parte;
        if (parte == 2U)
        {
            p2_arrancar();
        }
        else
        {
            p2_detener();
        }
        actualizar_leds();
        enviar_ayuda();
    }
}

static void ejecutar_comando(const char *comando)
{
    const char letra   = (char)((comando[0] >= 'a' && comando[0] <= 'z') ? comando[0] - 32 : comando[0]);
    const float valor  = (float)atof(&comando[1]);
    const long entero  = atol(&comando[1]);

    if (letra == '1' || letra == '2')
    {
        cambiar_parte((uint32_t)(letra - '0'));
    }
    else if (letra == '?')
    {
        enviar_ayuda();
    }
    else if (parte_activa == 1U)
    {
        switch (letra)
        {
            case 'U': if (valor > 0.0f && valor < 1.0f) { p1_mu = valor; } break;
            case 'A': if (valor > 0.0f) { p1_amplitud = (valor > 1.0f) ? 1.0f : valor; } break;
            case 'E': p1_entrada_constante = (entero != 0); break;
            case 'V': p1_ruido_medicion = (valor > 0.0f) ? valor : 0.0f; break;
            case 'N': if (entero >= 100) { p1_cant_muestras = (uint32_t)entero; } break;
            case 'R': p1_semilla = (uint32_t)entero; break;
            case 'D': p1_enviar_diferencias = (entero != 0); break;
            case 'S': p1_pedido_correr = true; break;
            case 'B': p1_pedido_barrido = true; break;
            default: break;
        }
    }
    else
    {
        switch (letra)
        {
            case 'U':   /* cambia mu sin borrar lo aprendido */
                if (valor > 0.0f && valor < 1.0f)
                {
                    p2_mu  = valor;
                    lms.mu = a_q15(valor);
                    p2_enviar_encabezado();
                }
                break;
            case 'A': if (valor > 0.0f) { p2_ruido_sigma = valor; p2_arrancar(); } break;
            case 'Q': p2_correlador_referencia = (entero != 0); p2_arrancar(); break;
            case 'O': if (entero >= 0 && entero < CANT_SENALES) { dac0_senal = (uint32_t)entero; } break;
            case 'P': if (entero >= 0 && entero < CANT_SENALES) { dac1_senal = (uint32_t)entero; } break;
            case 'Z': p2_reiniciar((entero > 0) ? (uint32_t)entero : 0U); break;
            case 'K': p2_capturar((entero > 0) ? (uint32_t)entero : 0U); break;
            case 'C': p2_enviar_coeficientes(); break;
            case 'T': p2_enviar_tiempos(); break;
            case 'M': p2_bloques_a_enviar = (entero > 0) ? (uint32_t)entero : 0U; break;
            default: break;
        }
    }
}


/*******************************************************************************
 * main
 ******************************************************************************/
int main(void)
{
    /* Pines, relojes y perifericos: ConfigTools, igual que TP2/TP3 */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlVref);   /* VREF antes del ADC (TP3) */
    BOARD_InitBootPeripherals();

    DAC_SetData(DAC0_PERIPHERAL, DAC_MITAD);
    DAC_SetData(DAC1_PERIPHERAL, DAC_MITAD);
    SysTick_Config(SystemCoreClock / 1000U);

    /* contador de ciclos para medir tiempos */
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* fs que resulta de la configuracion del CTIMER0 en ConfigTools (MAT3 conmuta) */
    fs_hz = (float)CTIMER0_TICK_FREQ / (2.0f * (float)(CTIMER0_Match_3_config.matchValue + 1U));

    /* Memoria compartida y arranque del core1 (MCMGR) */
    memset(compartida, 0, sizeof(memoria_compartida_t));
    __DMB();
    compartida->magico = VALOR_MAGICO;
    MCMGR_Init();
    MCMGR_StartCore(kMCMGR_Core1, (void *)DIRECCION_CORE1, DIRECCION_COMPARTIDA, kMCMGR_Start_Asynchronous);

    const uint32_t inicio = ms;
    while (!compartida->core1_listo && ((ms - inicio) < ESPERA_CORE1_MS))
    {
    }
    core1_funciona = compartida->core1_listo;

    actualizar_leds();
    enviar_ayuda();

    while (1)
    {
        char comando[CMD_LARGO];
        while (leer_comando(comando))
        {
            ejecutar_comando(comando);
        }

        if (sw3_apretado)
        {
            sw3_apretado = false;
            cambiar_parte((parte_activa == 1U) ? 2U : 1U);
        }

        if (parte_activa == 1U)
        {
            if (sw2_apretado)
            {
                sw2_apretado     = false;
                p1_pedido_correr = true;
            }
            if (p1_pedido_correr)
            {
                p1_pedido_correr = false;
                (void)p1_correr(p1_mu);
            }
            if (p1_pedido_barrido)
            {
                /* misma entrada (misma semilla), solo cambia mu */
                p1_pedido_barrido = false;
                enviar("P1B,INICIO\r\n");
                for (uint32_t i = 0U; i < CANT_MU_BARRIDO; i++)
                {
                    if (!p1_correr(mu_barrido[i]))
                    {
                        break;
                    }
                }
                enviar("P1B,FIN\r\n");
            }
        }
        else
        {
            if (sw2_apretado)
            {
                sw2_apretado = false;
                p2_reiniciar(0U);
            }
            p2_atender();
        }
    }
}
