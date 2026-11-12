/*
 * Corre la Parte 2 (parte2.c) en la PC simulando lo que llega al ADC:
 * una senoidal de amplitud A y frecuencia f0 cuantizada a 16 bits en 0..3,3 V.
 * Por stdout sale el mismo texto que manda la placa; si se da un archivo,
 * además se guardan las formas de onda (n, s, d, e, y) para graficar.
 *
 * Uso: p2_sim [alg] [mu] [ruido] [topo 0|1] [fs] [f0] [A] [segundos] [ondas.csv] [cada_cuanto]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "parte2.h"

static void wr(const char *s, size_t len)
{
    fwrite(s, 1, len, stdout);
}

int main(int argc, char **argv)
{
    p2_cfg_t cfg;
    p2_default_cfg(&cfg);
    float fs = 8000.0f, f0 = 200.0f, amp = 0.5f, secs = 3.0f;
    const char *ondas = NULL;
    unsigned dec = 1;
    if (argc > 1) cfg.alg = (lms_alg_t)atoi(argv[1]);
    if (argc > 2) cfg.mu = strtof(argv[2], NULL);
    if (argc > 3) cfg.noise_amp = strtof(argv[3], NULL);
    if (argc > 4) cfg.topo = (p2_topo_t)atoi(argv[4]);
    if (argc > 5) fs = strtof(argv[5], NULL);
    if (argc > 6) f0 = strtof(argv[6], NULL);
    if (argc > 7) amp = strtof(argv[7], NULL);
    if (argc > 8) secs = strtof(argv[8], NULL);
    if (argc > 9) ondas = argv[9];
    if (argc > 10) dec = (unsigned)atoi(argv[10]);

    FILE *fo = ondas ? fopen(ondas, "w") : NULL;
    if (fo) fprintf(fo, "n,s,d,e,y\n");

    p2_init(&cfg);
    p2_print_header(wr, fs);
    const unsigned n_tot = (unsigned)(secs * fs);
    double dc = 1.65;
    for (unsigned n = 0; n < n_tot; n++)
    {
        /* lo que mide el ADC: 1,65 V + A sin(), cuantizado a 16 bits */
        double v = 1.65 + amp * sin(2.0 * M_PI * f0 * n / fs);
        unsigned code = (unsigned)(v / 3.3 * 65536.0);
        if (code > 65535u) code = 65535u;
        float volts = (float)code * (3.3f / 65536.0f);
        /* mismo seguimiento de la continua que el firmware */
        dc += (volts - dc) * (1.0 / 4096.0);
        float s = volts - (float)dc;
        float out[P2_OUT_CNT];
        if (p2_process(s, out))
        {
            p2_stats_t st;
            p2_get_stats(&st);
            p2_print_stats(wr, &st);
        }
        if (fo && (n % dec) == 0)
            fprintf(fo, "%u,%.6f,%.6f,%.6f,%.6f\n", n, out[P2_OUT_S], out[P2_OUT_D], out[P2_OUT_E], out[P2_OUT_Y]);
    }
    p2_print_w(wr);
    if (fo) fclose(fo);
    return 0;
}
