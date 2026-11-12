/*
 * TP4 - Procesamiento Digital de Señales
 * lms.h: filtro transversal adaptativo (combinador lineal de entrada única).
 *
 * Tres implementaciones con la misma interfaz, para poder compararlas:
 *
 *   LMS_ALG_CLASE : la rutina LMSfir() de las diapositivas de clase,
 *                   escrita en C "a mano".
 *   LMS_ALG_CMSIS : arm_lms_f32() de CMSIS-DSP.
 *   LMS_ALG_NLMS  : arm_lms_norm_f32() de CMSIS-DSP (LMS normalizado).
 *
 * Las tres usan la misma convención que CMSIS-DSP y que la diapositiva:
 *
 *     y(n)   = sum_k w_k(n) x(n-k)
 *     e(n)   = d(n) - y(n)
 *     w_k(n+1) = w_k(n) + mu * e(n) * x(n-k)                 (LMS)
 *     w_k(n+1) = w_k(n) + mu * e(n) * x(n-k) / E(n)          (NLMS)
 *     E(n)   = x(n)^2 + x(n-1)^2 + ... + x(n-L+1)^2
 *
 * OJO: en la teoría (Widrow) el paso se escribe W(k+1) = W(k) + 2*mu*e*X,
 * así que el mu de este código equivale a 2*mu de la teoría. Con esta
 * convención la condición de convergencia queda 0 < mu < 2 / (L * Px).
 */
#ifndef LMS_H_
#define LMS_H_

#include <stdint.h>

#include "arm_math.h"

#define LMS_MAX_TAPS 32U

typedef enum
{
    LMS_ALG_CLASE = 0,
    LMS_ALG_CMSIS = 1,
    LMS_ALG_NLMS  = 2,
} lms_alg_t;

typedef struct
{
    lms_alg_t alg;
    uint16_t L;
    float32_t mu;

    /* LMS_ALG_CLASE: x_hist[0] = x(n), x_hist[1] = x(n-1), ...; w en orden natural */
    float32_t x_hist[LMS_MAX_TAPS];
    float32_t w[LMS_MAX_TAPS];

    /* CMSIS-DSP: coeficientes en orden invertido en el tiempo y estado de
     * L + blockSize - 1 muestras (acá blockSize = 1: muestra a muestra). */
    float32_t cm_coef[LMS_MAX_TAPS];
    float32_t cm_state[LMS_MAX_TAPS];
    arm_lms_instance_f32 cm;
    arm_lms_norm_instance_f32 nm;
} lms_t;

/* Inicializa con los coeficientes en cero */
void lms_init(lms_t *f, lms_alg_t alg, uint16_t L, float32_t mu);

/* Vuelve los coeficientes y la historia de la entrada a cero (re-aprendizaje) */
void lms_reset(lms_t *f);

/* Cambia mu sin tocar los coeficientes */
void lms_set_mu(lms_t *f, float32_t mu);

/* Procesa una muestra: x = entrada (referencia), d = salida deseada.
 * Devuelve y (salida del filtro) y escribe el error en *e. */
float32_t lms_step(lms_t *f, float32_t x, float32_t d, float32_t *e);

/* Copia los coeficientes en orden natural: w[0] multiplica a x(n) */
void lms_get_w(const lms_t *f, float32_t *w);

const char *lms_alg_name(lms_alg_t alg);

#endif /* LMS_H_ */
