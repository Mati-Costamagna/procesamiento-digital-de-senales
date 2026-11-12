/*
 * TP4 - Procesamiento Digital de Señales
 * lms.c: filtro transversal adaptativo LMS / NLMS (ver lms.h).
 */
#include "lms.h"

#include <string.h>

void lms_init(lms_t *f, lms_alg_t alg, uint16_t L, float32_t mu)
{
    if (L > LMS_MAX_TAPS)
    {
        L = (uint16_t)LMS_MAX_TAPS;
    }
    f->alg = alg;
    f->L   = L;
    f->mu  = mu;
    lms_reset(f);
}

void lms_reset(lms_t *f)
{
    (void)memset(f->x_hist, 0, sizeof(f->x_hist));
    (void)memset(f->w, 0, sizeof(f->w));
    (void)memset(f->cm_coef, 0, sizeof(f->cm_coef));
    (void)memset(f->cm_state, 0, sizeof(f->cm_state));

    /* arm_lms_*_init pone el estado en cero; los coeficientes los pone el usuario */
    arm_lms_init_f32(&f->cm, f->L, f->cm_coef, f->cm_state, f->mu, 1U);
    arm_lms_norm_init_f32(&f->nm, f->L, f->cm_coef, f->cm_state, f->mu, 1U);
}

void lms_set_mu(lms_t *f, float32_t mu)
{
    f->mu    = mu;
    f->cm.mu = mu;
    f->nm.mu = mu;
}

/*
 * Rutina de la diapositiva "Implementación en DSP" (segunda versión, la que
 * desplaza la historia, adapta y acumula en un solo lazo). Se mantiene la
 * misma estructura; sólo cambia que el error se calcula ANTES de adaptar,
 * con la salida del filtro de la muestra actual, como en arm_lms_f32. En la
 * versión de la diapositiva el (d - y) del lazo usa la y que se va
 * acumulando, que no es el error de esta muestra.
 */
static float32_t lms_clase_step(lms_t *f, float32_t xin, float32_t d, float32_t *e_out)
{
    const int L     = (int)f->L;
    float32_t *x    = f->x_hist;
    float32_t *w    = f->w;
    float32_t y     = 0.0f;
    int i;

    /* 1) actualizar la historia de la entrada (línea de retardo) */
    for (i = L - 1; i >= 1; i--)
    {
        x[i] = x[i - 1];
    }
    x[0] = xin;

    /* 2) salida del FIR con los coeficientes actuales */
    for (i = L - 1; i >= 0; i--)
    {
        y += w[i] * x[i];
    }

    /* 3) error y adaptación de los coeficientes (LMS) */
    const float32_t e   = d - y;
    const float32_t mue = f->mu * e;
    for (i = L - 1; i >= 0; i--)
    {
        w[i] += mue * x[i];
    }

    *e_out = e;
    return y;
}

float32_t lms_step(lms_t *f, float32_t x, float32_t d, float32_t *e)
{
    float32_t y;

    switch (f->alg)
    {
        case LMS_ALG_CMSIS:
            arm_lms_f32(&f->cm, &x, &d, &y, e, 1U);
            break;
        case LMS_ALG_NLMS:
            arm_lms_norm_f32(&f->nm, &x, &d, &y, e, 1U);
            break;
        case LMS_ALG_CLASE:
        default:
            y = lms_clase_step(f, x, d, e);
            break;
    }
    return y;
}

void lms_get_w(const lms_t *f, float32_t *w)
{
    const uint32_t L = f->L;

    if (f->alg == LMS_ALG_CLASE)
    {
        (void)memcpy(w, f->w, L * sizeof(float32_t));
    }
    else
    {
        /* CMSIS guarda los coeficientes en orden invertido: cm_coef[L-1] multiplica a x(n) */
        for (uint32_t k = 0U; k < L; k++)
        {
            w[k] = f->cm_coef[L - 1U - k];
        }
    }
}

const char *lms_alg_name(lms_alg_t alg)
{
    switch (alg)
    {
        case LMS_ALG_CMSIS:
            return "CMSIS_LMS";
        case LMS_ALG_NLMS:
            return "CMSIS_NLMS";
        case LMS_ALG_CLASE:
        default:
            return "LMS_CLASE";
    }
}
