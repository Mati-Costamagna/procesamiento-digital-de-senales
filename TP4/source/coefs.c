/*
 * TP4 - Procesamiento Digital de Señales
 * coefs.c: coeficientes fijos (generados en tools/host_sim/gen_coefs.py).
 */
#include "coefs.h"

/* p[k] = 0,8 e^(-k/8) sin(0,3 pi (k+1)) + 0,05 cos(0,9 k), redondeado a 4 decimales */
const float g_planta[COEFS_L] = {
    0.6972f,  0.7025f,  0.1812f,  -0.3684f, -0.5301f, -0.2622f, 0.1485f,  0.3672f,
    0.2685f,  -0.0122f, -0.2310f, -0.2368f, -0.0649f, 0.1250f,  0.1890f,  0.1019f,
    -0.0464f, -0.1368f, -0.1123f, -0.0089f, 0.0861f,  0.1051f,  0.0449f,  -0.0403f,
    -0.0861f, -0.0643f, 0.0015f,  0.0597f,  0.0694f,  0.0284f,
};

/* c[k] = 0,6 * 0,8^k, redondeado a 4 decimales */
const float g_correlador[COEFS_L] = {
    0.6000f, 0.4800f, 0.3840f, 0.3072f, 0.2458f, 0.1966f, 0.1573f, 0.1258f,
    0.1007f, 0.0805f, 0.0644f, 0.0515f, 0.0412f, 0.0330f, 0.0264f, 0.0211f,
    0.0169f, 0.0135f, 0.0108f, 0.0086f, 0.0069f, 0.0055f, 0.0044f, 0.0035f,
    0.0028f, 0.0023f, 0.0018f, 0.0015f, 0.0012f, 0.0009f,
};
