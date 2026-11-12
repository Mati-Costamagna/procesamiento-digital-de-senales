/*
 * Corre la Parte 1 (parte1.c) en la PC, con la misma CMSIS-DSP que la placa.
 * La salida es exactamente el mismo texto que manda la placa por el serie,
 * así que sirve para probar los scripts de MATLAB sin la placa.
 *
 * Uso: p1_sim [alg 0|1|2] [mu] [in 0=random|1=const] [A] [sigma_v] [N] [seed] [diff 0|1]
 */
#include <stdio.h>
#include <stdlib.h>

#include "parte1.h"

static void wr(const char *s, size_t len)
{
    fwrite(s, 1, len, stdout);
}

int main(int argc, char **argv)
{
    p1_cfg_t cfg;
    p1_default_cfg(&cfg);
    if (argc > 1) cfg.alg = (lms_alg_t)atoi(argv[1]);
    if (argc > 2) cfg.mu = strtof(argv[2], NULL);
    if (argc > 3) cfg.input = (p1_input_t)atoi(argv[3]);
    if (argc > 4) cfg.amp = strtof(argv[4], NULL);
    if (argc > 5) cfg.sigma_v = strtof(argv[5], NULL);
    if (argc > 6) cfg.n_total = (uint32_t)strtoul(argv[6], NULL, 10);
    if (argc > 7) cfg.seed = (uint32_t)strtoul(argv[7], NULL, 10);
    if (argc > 8) cfg.send_diff = atoi(argv[8]) != 0;

    const p1_io_t io = {wr, NULL, NULL};
    return (int)p1_run(&cfg, &io);
}
