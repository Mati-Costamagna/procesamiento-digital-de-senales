/*
 * TP4 - Procesamiento Digital de Señales
 * parte2.h: Parte 2 - Análisis de la influencia del ruido (cancelador adaptativo).
 *
 * Topología por defecto (P2_CORR_EN_RUIDO), la del "cancelador de
 * interferencia" de la teoría:
 *
 *   generador ──► ADC ──► s(n) ─────────────────(+)──► d(n) = s(n) + n'(n) ─(+)──► e(n) ──► DAC ─► osc.
 *                                                ▲                           ▲ -     (≈ s(n))
 *   random r(n) ──► CORRELADOR C(z) ──► n'(n) ───┘                           │
 *        │          (FIR 30 coef. fijos)                                     │ y(n) (≈ n'(n))
 *        └──────────────────────────────► x(n) = r(n) ──► FIR ADAPTATIVO ────┘
 *                                                          W(z), 30 coef., LMS
 *
 *   El ruido que contamina la señal (n') y la referencia (r) están correlacionados
 *   a través de C(z). El filtro óptimo (Wiener) es W = C: el procesador "copia"
 *   al correlador y la salida e(n) = s(n) + n'(n) - y(n) tiende a s(n).
 *
 * Topología alternativa (P2_CORR_EN_REFERENCIA), la que está dibujada en la
 * figura 2 del enunciado: el ruido entra directo a d(n) = s(n) + r(n) y el
 * correlador está en la rama de la referencia, x(n) = C{r}(n). Ahora el
 * óptimo es W = C^-1. Como C(z) = 0,6/(1 - 0,8 z^-1) (truncado a 30
 * coeficientes) es de fase mínima, su inversa es estable y prácticamente un
 * FIR de 2 coeficientes: W ≈ [1/0,6, -0,8/0,6, 0, ...] = [1,667, -1,333, 0, ...].
 *
 * Como el ruido se genera adentro de la placa, se conoce s(n) "limpia" y se
 * puede medir exactamente cuánto ruido queda: residuo(n) = e(n) - s(n).
 *
 * Este módulo no toca hardware: se llama una vez por muestra desde la ISR del
 * ADC en la placa, o desde tools/host_sim/p2_sim.c en la PC.
 */
#ifndef PARTE2_H_
#define PARTE2_H_

#include <stdbool.h>
#include <stdint.h>

#include "lms.h"
#include "txt_out.h"

typedef enum
{
    P2_CORR_EN_RUIDO      = 0,
    P2_CORR_EN_REFERENCIA = 1,
} p2_topo_t;

/* Señal que sale por el DAC (para el osciloscopio) */
typedef enum
{
    P2_OUT_D   = 0, /* señal + ruido (entrada "sucia" del cancelador) */
    P2_OUT_E   = 1, /* error = salida del cancelador (señal recuperada) */
    P2_OUT_Y   = 2, /* salida del FIR adaptativo (estimación del ruido) */
    P2_OUT_S   = 3, /* señal del generador sola (después de sacar la continua) */
    P2_OUT_RES = 4, /* ruido residual e - s (amplificado x4) */
    P2_OUT_CNT = 5,
} p2_out_t;

typedef struct
{
    lms_alg_t alg;
    float mu;
    float noise_amp; /* r(n) uniforme en [-a, a] */
    p2_topo_t topo;
    uint32_t seed;
    uint32_t block;  /* muestras por bloque de estadísticas (100) */
} p2_cfg_t;

/* Estadísticas de un bloque de 'block' muestras */
typedef struct
{
    uint32_t n;      /* índice de la última muestra del bloque */
    float p_noise;   /* potencia del ruido que contamina la señal (d - s) */
    float p_res;     /* potencia del ruido residual (e - s) */
    float p_sig;     /* potencia de la señal s */
    float werr2;     /* ||w_opt - w||^2 */
} p2_stats_t;

void p2_default_cfg(p2_cfg_t *cfg);

/* Inicializa (coeficientes en cero). Se puede volver a llamar para cambiar la config. */
void p2_init(const p2_cfg_t *cfg);
const p2_cfg_t *p2_get_cfg(void);

/* Vuelve los coeficientes a cero: el filtro empieza a aprender de nuevo */
void p2_reset_w(void);
void p2_set_mu(float mu);

/* Procesa una muestra de la señal s (en volts, sin continua). Escribe las
 * P2_OUT_CNT salidas en out[]. Devuelve true cuando se completó un bloque de
 * estadísticas (se leen con p2_get_stats). */
bool p2_process(float s, float out[P2_OUT_CNT]);
void p2_get_stats(p2_stats_t *st);

/* Coeficientes actuales (orden natural) y los óptimos teóricos */
void p2_get_w(float *w);
void p2_get_w_opt(float *w_opt);

const char *p2_out_name(p2_out_t o);

/*
 * Líneas de texto que manda la placa (y la simulación) en la parte 2:
 *  P2I,alg=..,mu=..,ruido=..,topo=RUIDO|REFERENCIA,fs=..,blk=..,mu_max=..,tau=..
 *  P2M,n,p_ruido,p_residuo,reduccion_dB,werr2,snr_entrada_dB,snr_salida_dB
 *  P2W,w0,...,w29        coeficientes actuales (comando C)
 *  P2O,w0,...,w29        coeficientes óptimos teóricos
 */
void p2_print_header(txt_write_fn wr, float fs);
void p2_print_stats(txt_write_fn wr, const p2_stats_t *st);
void p2_print_w(txt_write_fn wr);

#endif /* PARTE2_H_ */
