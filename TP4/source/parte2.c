/*
 * TP4 - Procesamiento Digital de Señales
 * parte2.c: cancelador adaptativo de ruido (ver parte2.h).
 */
#include "parte2.h"

#include <math.h>
#include <string.h>

#include "coefs.h"
#include "fir_fijo.h"
#include "prng.h"

static p2_cfg_t s_cfg;
static lms_t s_lms;
static fir_fijo_t s_corr;
static prng_t s_rng;
static float s_w_opt[COEFS_L];

/* Acumuladores del bloque en curso y último bloque terminado */
static uint32_t s_n;
static uint32_t s_b;
static float s_acc_noise;
static float s_acc_res;
static float s_acc_sig;
static p2_stats_t s_last;

void p2_default_cfg(p2_cfg_t *cfg)
{
    cfg->alg       = LMS_ALG_CMSIS;
    cfg->mu        = 0.005f;
    cfg->noise_amp = 0.4f;
    cfg->topo      = P2_CORR_EN_RUIDO;
    cfg->seed      = 1U;
    cfg->block     = 100U;
}

static void compute_w_opt(void)
{
    (void)memset(s_w_opt, 0, sizeof(s_w_opt));
    if (s_cfg.topo == P2_CORR_EN_RUIDO)
    {
        /* Wiener: W = C */
        (void)memcpy(s_w_opt, g_correlador, sizeof(s_w_opt));
    }
    else
    {
        /* W = C^-1 con C(z) ≈ c0 / (1 - a z^-1): W(z) ≈ (1 - a z^-1) / c0 */
        const float c0 = g_correlador[0];
        const float a  = g_correlador[1] / g_correlador[0];
        s_w_opt[0]     = 1.0f / c0;
        s_w_opt[1]     = -a / c0;
    }
}

void p2_init(const p2_cfg_t *cfg)
{
    s_cfg = *cfg;
    if (s_cfg.block == 0U)
    {
        s_cfg.block = 100U;
    }
    lms_init(&s_lms, s_cfg.alg, (uint16_t)COEFS_L, s_cfg.mu);
    fir_fijo_init(&s_corr, g_correlador, COEFS_L);
    prng_seed(&s_rng, s_cfg.seed);
    compute_w_opt();

    s_n         = 0U;
    s_b         = 0U;
    s_acc_noise = 0.0f;
    s_acc_res   = 0.0f;
    s_acc_sig   = 0.0f;
    (void)memset(&s_last, 0, sizeof(s_last));
}

const p2_cfg_t *p2_get_cfg(void)
{
    return &s_cfg;
}

void p2_reset_w(void)
{
    lms_reset(&s_lms);
}

void p2_set_mu(float mu)
{
    s_cfg.mu = mu;
    lms_set_mu(&s_lms, mu);
}

bool p2_process(float s, float out[P2_OUT_CNT])
{
    /* 1) ruido aleatorio y su versión correlacionada */
    const float r  = s_cfg.noise_amp * prng_uniform(&s_rng);
    const float rc = fir_fijo_step(&s_corr, r);

    float noise; /* ruido que contamina la señal */
    float ref;   /* referencia del procesador adaptativo */
    if (s_cfg.topo == P2_CORR_EN_RUIDO)
    {
        noise = rc;
        ref   = r;
    }
    else
    {
        noise = r;
        ref   = rc;
    }

    /* 2) señal + ruido y cancelador LMS */
    const float d = s + noise;
    float e;
    const float y = lms_step(&s_lms, ref, d, &e);

    const float res = e - s;
    out[P2_OUT_D]   = d;
    out[P2_OUT_E]   = e;
    out[P2_OUT_Y]   = y;
    out[P2_OUT_S]   = s;
    out[P2_OUT_RES] = 4.0f * res;

    /* 3) estadísticas por bloque */
    s_acc_noise += noise * noise;
    s_acc_res += res * res;
    s_acc_sig += s * s;
    s_n++;
    if (++s_b < s_cfg.block)
    {
        return false;
    }

    const float inv = 1.0f / (float)s_b;
    s_last.n        = s_n;
    s_last.p_noise  = s_acc_noise * inv;
    s_last.p_res    = s_acc_res * inv;
    s_last.p_sig    = s_acc_sig * inv;
    s_b             = 0U;
    s_acc_noise     = 0.0f;
    s_acc_res       = 0.0f;
    s_acc_sig       = 0.0f;

    /* Error de los coeficientes respecto del óptimo (sólo cada bloque: es caro) */
    float w[COEFS_L];
    float acc = 0.0f;
    lms_get_w(&s_lms, w);
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        const float dk = s_w_opt[k] - w[k];
        acc += dk * dk;
    }
    s_last.werr2 = acc;
    return true;
}

void p2_get_stats(p2_stats_t *st)
{
    *st = s_last;
}

void p2_get_w(float *w)
{
    lms_get_w(&s_lms, w);
}

void p2_get_w_opt(float *w_opt)
{
    (void)memcpy(w_opt, s_w_opt, sizeof(s_w_opt));
}

const char *p2_out_name(p2_out_t o)
{
    static const char *const names[P2_OUT_CNT] = {"d(senal+ruido)", "e(salida)", "y(ruido_estimado)",
                                                  "s(senal)", "residuo_x4"};
    return (o < P2_OUT_CNT) ? names[o] : "?";
}

static float db10(float x)
{
    if (!(x > 0.0f))
    {
        return -999.0f;
    }
    return 10.0f * 0.30102999566f * log2f(x);
}

void p2_print_header(txt_write_fn wr, float fs)
{
    txt_line_t ln;
    /* Potencia de la referencia: uniforme -> a^2/3; si pasa por C, sum(c^2) = 1 */
    const float px = s_cfg.noise_amp * s_cfg.noise_amp / 3.0f;
    float mu_max   = 2.0f / ((float)COEFS_L * px);
    float tau      = 1.0f / (2.0f * s_cfg.mu * px);
    if (s_cfg.alg == LMS_ALG_NLMS)
    {
        mu_max = 2.0f;
        tau    = (float)COEFS_L / (2.0f * s_cfg.mu);
    }

    txt_begin(&ln, "P2I");
    txt_kv_str(&ln, "alg", lms_alg_name(s_cfg.alg));
    txt_kv_f32(&ln, "mu", s_cfg.mu);
    txt_kv_f32(&ln, "ruido", s_cfg.noise_amp);
    txt_kv_str(&ln, "topo", (s_cfg.topo == P2_CORR_EN_RUIDO) ? "RUIDO" : "REFERENCIA");
    txt_kv_f32(&ln, "fs", fs);
    txt_kv_u32(&ln, "blk", s_cfg.block);
    txt_kv_f32(&ln, "mu_max", mu_max);
    txt_kv_f32(&ln, "tau", tau);
    txt_end(&ln, wr);
}

void p2_print_stats(txt_write_fn wr, const p2_stats_t *st)
{
    txt_line_t ln;
    txt_begin(&ln, "P2M");
    txt_u32(&ln, st->n);
    txt_f32(&ln, st->p_noise);
    txt_f32(&ln, st->p_res);
    txt_f32(&ln, db10(st->p_noise) - db10(st->p_res));
    txt_f32(&ln, st->werr2);
    txt_f32(&ln, db10(st->p_sig) - db10(st->p_noise));
    txt_f32(&ln, db10(st->p_sig) - db10(st->p_res));
    txt_end(&ln, wr);
}

void p2_print_w(txt_write_fn wr)
{
    txt_line_t ln;
    float w[COEFS_L];

    p2_get_w(w);
    txt_begin(&ln, "P2W");
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        txt_f32(&ln, w[k]);
    }
    txt_end(&ln, wr);

    txt_begin(&ln, "P2O");
    for (uint32_t k = 0U; k < COEFS_L; k++)
    {
        txt_f32(&ln, s_w_opt[k]);
    }
    txt_end(&ln, wr);
}
