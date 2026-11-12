/*
 * TP4 - Procesamiento Digital de Señales
 * prng.h: generador pseudoaleatorio xorshift32 (Marsaglia).
 *
 * Se usa uno propio en lugar de rand() para que la secuencia sea idéntica en
 * la placa y en la simulación de la PC, y para poder repetir un experimento
 * con la misma semilla cambiando sólo mu.
 */
#ifndef PRNG_H_
#define PRNG_H_

#include <stdint.h>

typedef struct
{
    uint32_t s;
} prng_t;

static inline void prng_seed(prng_t *p, uint32_t seed)
{
    p->s = (seed != 0U) ? seed : 0x12345678U; /* el estado nunca puede ser 0 */
}

static inline uint32_t prng_u32(prng_t *p)
{
    uint32_t x = p->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->s = x;
    return x;
}

/* Uniforme en [-1, 1): media 0, varianza 1/3 */
static inline float prng_uniform(prng_t *p)
{
    /* 24 bits de mantisa: (u >> 8) en [0, 2^24) */
    return (float)(int32_t)((prng_u32(p) >> 8) - 0x800000U) * (1.0f / 8388608.0f);
}

#endif /* PRNG_H_ */
