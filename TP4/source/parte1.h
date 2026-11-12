/*
 * TP4 - Procesamiento Digital de Señales
 * parte1.h: Parte 1 - Análisis de un proceso adaptativo (identificación de planta).
 *
 *              +-------------------+  d(n)
 *   x(n) --+-->| PLANTA P(z), FIR  |---(+)---> (+)--> e(n)
 *          |   | 30 coef. fijos    |    ^ v(n)  ^ -
 *          |   +-------------------+    |       |
 *          |   +-------------------+    |       | y(n)
 *          +-->| FIR ADAPTATIVO    |----+-------+
 *              | 30 coef. w(n)     |<-- LMS (e, x)
 *              +-------------------+
 *
 *  x(n): entrada pseudoaleatoria uniforme en [-A, A] (Px = A^2/3) o constante A (Px = A^2)
 *  v(n): ruido de medición opcional (desvío sigma_v); con v = 0 el MSE mínimo es 0
 *
 * Este módulo no toca hardware: el mismo código corre en la placa y en la
 * simulación de la PC (tools/host_sim). Todo lo que muestra sale como
 * líneas de texto CSV por la función de escritura que se le pasa:
 *
 *  P1I,alg=..,mu=..,in=RANDOM|CONST,A=..,Px=..,L=..,N=..,blk=..,sv=..,seed=..,mu_max=..,tau=..
 *  P1P,p0,...,p29                  coeficientes de la planta
 *  P1M,n,mse,werr2,mse_dB          cada 100 muestras: MSE promedio del bloque y
 *                                  ||p - w||^2 (error de los coeficientes)
 *  P1D,n,p0-w0,...,p29-w29         cada 100 muestras (si diff = 1): diferencia de cada coeficiente
 *  P1W,w0,...,w29                  coeficientes del filtro adaptativo al final
 *  P1F,n,mse,werr2,cyc,estado      fin: ciclos promedio por muestra del LMS y
 *                                  estado = OK | DIVERGE | CORTADO
 */
#ifndef PARTE1_H_
#define PARTE1_H_

#include <stdbool.h>
#include <stdint.h>

#include "lms.h"
#include "txt_out.h"

typedef enum
{
    P1_IN_RANDOM = 0,
    P1_IN_CONST  = 1,
} p1_input_t;

typedef struct
{
    lms_alg_t alg;
    float mu;
    p1_input_t input;
    float amp;        /* A: amplitud de la entrada */
    float sigma_v;    /* desvío del ruido de medición sumado a d(n) */
    uint32_t n_total; /* muestras a simular */
    uint32_t block;   /* muestras por promedio de MSE (100 según la consigna) */
    uint32_t seed;
    bool send_diff;   /* manda la diferencia de los 30 coeficientes cada bloque */
} p1_cfg_t;

typedef struct
{
    txt_write_fn write;       /* salida de texto (UART o stdout) */
    uint32_t (*cycles)(void); /* contador de ciclos (DWT->CYCCNT); puede ser NULL */
    bool (*abort)(void);      /* devuelve true para cortar la corrida; puede ser NULL */
} p1_io_t;

typedef enum
{
    P1_OK      = 0,
    P1_DIVERGE = 1,
    P1_CORTADO = 2,
} p1_status_t;

void p1_default_cfg(p1_cfg_t *cfg);

/* Corre un experimento completo (bloqueante). */
p1_status_t p1_run(const p1_cfg_t *cfg, const p1_io_t *io);

/* Límite teórico de mu y constante de tiempo del MSE para una configuración */
float p1_input_power(const p1_cfg_t *cfg);
float p1_mu_max(const p1_cfg_t *cfg);

#endif /* PARTE1_H_ */
