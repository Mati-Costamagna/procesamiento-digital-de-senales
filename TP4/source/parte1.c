/*
 * TP4 - Procesamiento Digital de Señales
 * parte1.c: identificación de planta con LMS (ver parte1.h).
 */
#include "parte1.h"

#include <math.h>

#include "coefs.h"
#include "fir_fijo.h"
#include "prng.h"

static lms_t s_lms;
static fir_fijo_t s_planta;

void p1_default_cfg(p1_cfg_t *cfg)
{
    cfg->alg       = LMS_ALG_CMSIS;
    cfg->mu        = 0.01f;
    cfg->input     = P1_IN_RANDOM;
    cfg->amp       = 1.0f;
    cfg->sigma_v   = 0.0f;
    cfg->n_total   = 20000U;
    cfg->block     = 100U;
    cfg->seed      = 1U;
    cfg->send_diff = true;
}

float p1_input_power(const p1_cfg_t *cfg)
{
    const float a2 = cfg->amp * cfg->amp;
    return (cfg->input == P1_IN_CONST) ? a2 : (a2 / 3.0f);
}

/*
 * Condición de convergencia "segura" de la teoría: 0 < 2mu_teo < 2/tr(R), con
 * tr(R) = L * Px. Con la convención de este código (mu = 2 mu_teo):
 *     LMS : 0 < mu < 2 / (L * Px)
 *     NLMS: 0 < mu < 2 (no depende de la potencia de la entrada)
 * Vale tanto para la entrada aleatoria (R = Px I) como para la constante
 * (R = A^2 * 1 1^T, un solo autovalor no nulo, igual a L A^2 = tr(R)).
 */
float p1_mu_max(const p1_cfg_t *cfg)
{
    if (cfg->alg == LMS_ALG_NLMS)
    {
        return 2.0f;
    }
    return 2.0f / ((float)COEFS_L * p1_input_power(cfg));
}

/*
 * Constante de tiempo de la curva de aprendizaje (MSE), en muestras:
 *   el modo p del error de los pesos decae como (1 - mu lambda_p)^n, y el
 *   MSE como su cuadrado -> tau_mse ~ 1 / (2 mu lambda_p).
 *   Aleatoria blanca: todos los lambda = Px. Constante: lambda = L A^2.
 *   NLMS: el paso efectivo es mu / (L Px).
 */
static float p1_tau(const p1_cfg_t *cfg)
{
    float lambda = (cfg->input == P1_IN_CONST) ? ((float)COEFS_L * p1_input_power(cfg)) : p1_input_power(cfg);
    float mu_ef  = cfg->mu;
    if (cfg->alg == LMS_ALG_NLMS)
    {
        mu_ef = cfg->mu / ((float)COEFS_L * p1_input_power(cfg));
    }
    const float den = 2.0f * mu_ef * lambda;
    return (den > 0.0f) ? (1.0f / den) : 0.0f;
}

static float coef_error(float *diff, const float *w)
{
    float acc = 0.0f;
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        diff[k] = g_planta[k] - w[k];
        acc += diff[k] * diff[k];
    }
    return acc;
}

/* 10 log10(x) sin math.h de punto flotante doble (alcanza con ~0,01 dB) */
static float db10(float x)
{
    if (!(x > 0.0f))
    {
        return -999.0f;
    }
    return 10.0f * 0.30102999566f * log2f(x);
}

p1_status_t p1_run(const p1_cfg_t *cfg, const p1_io_t *io)
{
    txt_line_t ln;
    float w[COEFS_L];
    float diff[COEFS_L];
    prng_t rx;
    prng_t rv;
    p1_status_t st   = P1_OK;
    uint32_t cyc_acc = 0U;
    uint32_t n       = 0U;
    float mse        = 0.0f;
    float werr2      = 0.0f;
    const float sv   = cfg->sigma_v * 1.7320508f; /* uniforme [-a, a] tiene desvío a/sqrt(3) */

    lms_init(&s_lms, cfg->alg, (uint16_t)COEFS_L, cfg->mu);
    fir_fijo_init(&s_planta, g_planta, COEFS_L);
    prng_seed(&rx, cfg->seed);
    prng_seed(&rv, cfg->seed ^ 0xA5A5A5A5U);

    /* Encabezado del experimento */
    txt_begin(&ln, "P1I");
    txt_kv_str(&ln, "alg", lms_alg_name(cfg->alg));
    txt_kv_f32(&ln, "mu", cfg->mu);
    txt_kv_str(&ln, "in", (cfg->input == P1_IN_CONST) ? "CONST" : "RANDOM");
    txt_kv_f32(&ln, "A", cfg->amp);
    txt_kv_f32(&ln, "Px", p1_input_power(cfg));
    txt_kv_u32(&ln, "L", COEFS_L);
    txt_kv_u32(&ln, "N", cfg->n_total);
    txt_kv_u32(&ln, "blk", cfg->block);
    txt_kv_f32(&ln, "sv", cfg->sigma_v);
    txt_kv_u32(&ln, "seed", cfg->seed);
    txt_kv_f32(&ln, "mu_max", p1_mu_max(cfg));
    txt_kv_f32(&ln, "tau", p1_tau(cfg));
    txt_end(&ln, io->write);

    txt_begin(&ln, "P1P");
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        txt_f32(&ln, g_planta[k]);
    }
    txt_end(&ln, io->write);

    while (n < cfg->n_total)
    {
        float acc = 0.0f;
        uint32_t b;

        /* --- un bloque de 'block' muestras --- */
        for (b = 0U; (b < cfg->block) && (n < cfg->n_total); b++, n++)
        {
            /* entrada */
            const float x = (cfg->input == P1_IN_CONST) ? cfg->amp : (cfg->amp * prng_uniform(&rx));

            /* planta (+ ruido de medición) -> salida deseada */
            float d = fir_fijo_step(&s_planta, x);
            if (sv > 0.0f)
            {
                d += sv * prng_uniform(&rv);
            }

            /* procesador adaptativo */
            float e;
            const uint32_t c0 = (io->cycles != NULL) ? io->cycles() : 0U;
            (void)lms_step(&s_lms, x, d, &e);
            if (io->cycles != NULL)
            {
                cyc_acc += io->cycles() - c0;
            }
            acc += e * e;
        }

        /* --- cada bloque: MSE promedio y error de los coeficientes --- */
        mse = acc / (float)b;
        lms_get_w(&s_lms, w);
        werr2 = coef_error(diff, w);

        txt_begin(&ln, "P1M");
        txt_u32(&ln, n);
        txt_f32(&ln, mse);
        txt_f32(&ln, werr2);
        txt_f32(&ln, db10(mse));
        txt_end(&ln, io->write);

        if (cfg->send_diff)
        {
            txt_begin(&ln, "P1D");
            txt_u32(&ln, n);
            for (uint32_t k = 0U; k < COEFS_L; k++)
            {
                txt_f32(&ln, diff[k]);
            }
            txt_end(&ln, io->write);
        }

        /* Si el MSE explota (mu demasiado grande) no tiene sentido seguir */
        if (!(mse < 1.0e12f) || !(werr2 < 1.0e12f))
        {
            st = P1_DIVERGE;
            break;
        }
        if ((io->abort != NULL) && io->abort())
        {
            st = P1_CORTADO;
            break;
        }
    }

    txt_begin(&ln, "P1W");
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        txt_f32(&ln, w[k]);
    }
    txt_end(&ln, io->write);

    txt_begin(&ln, "P1F");
    txt_u32(&ln, n);
    txt_f32(&ln, mse);
    txt_f32(&ln, werr2);
    txt_u32(&ln, (n > 0U) ? (cyc_acc / n) : 0U);
    txt_str(&ln, (st == P1_OK) ? "OK" : ((st == P1_DIVERGE) ? "DIVERGE" : "CORTADO"));
    txt_end(&ln, io->write);

    return st;
}
