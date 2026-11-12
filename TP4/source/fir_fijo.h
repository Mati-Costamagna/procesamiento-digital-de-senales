/*
 * TP4 - Procesamiento Digital de Señales
 * fir_fijo.h: FIR de coeficientes fijos, muestra a muestra.
 *
 * Se usa para la PLANTA a identificar (parte 1) y para el CORRELADOR del
 * ruido (parte 2). Los dos son FIR de 30 coeficientes fijos:
 *
 *     y(n) = sum_{k=0}^{L-1} h[k] x(n-k)
 *
 * Los coeficientes están en orden natural (h[0] multiplica a x(n)), igual
 * que los que muestra la placa por el puerto serie y los que usa MATLAB.
 */
#ifndef FIR_FIJO_H_
#define FIR_FIJO_H_

#include <stdint.h>
#include <string.h>

#define FIR_FIJO_MAX 32U

typedef struct
{
    const float *h;
    uint32_t L;
    uint32_t pos;            /* posición de x(n) en el buffer circular */
    float x[FIR_FIJO_MAX];
} fir_fijo_t;

static inline void fir_fijo_init(fir_fijo_t *f, const float *h, uint32_t L)
{
    f->h   = h;
    f->L   = (L > FIR_FIJO_MAX) ? FIR_FIJO_MAX : L;
    f->pos = 0U;
    (void)memset(f->x, 0, sizeof(f->x));
}

static inline float fir_fijo_step(fir_fijo_t *f, float xin)
{
    uint32_t i = (f->pos == 0U) ? (f->L - 1U) : (f->pos - 1U);
    f->pos     = i;
    f->x[i]    = xin;

    float y = 0.0f;
    for (uint32_t k = 0U; k < f->L; k++)
    {
        y += f->h[k] * f->x[i];          /* h[k] * x(n-k) */
        i = (i + 1U == f->L) ? 0U : (i + 1U);
    }
    return y;
}

#endif /* FIR_FIJO_H_ */
