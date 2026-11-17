/*
 * TP4 - Procesamiento Digital de Señales
 * coefs.c: coeficientes fijos (generados en tools/host_sim/gen_coefs.py).
 */
#include "coefs.h"

/* p[k] = 0,8 e^(-k/3) sin(0,3 pi (k+1)) + 0,05 cos(0,9 k), redondeado a 4 decimales */
const float g_planta[COEFS_L] = {
    0.6972f, 0.5762f, 0.1156f, -0.2182f, -0.2557f, -0.0994f, 0.0652f, 0.1238f,
};

/* c[k] = sqrt(1 - 0,7^2) * 0,7^k, redondeado a 4 decimales */
const float g_correlador[COEFS_L] = {
    0.7141f, 0.4999f, 0.3499f, 0.2450f, 0.1715f, 0.1200f, 0.0840f, 0.0588f,
};
