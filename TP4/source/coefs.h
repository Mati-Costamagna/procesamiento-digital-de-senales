/*
 * TP4 - Procesamiento Digital de Señales
 * coefs.h: coeficientes FIJOS de la planta (parte 1) y del correlador (parte 2).
 */
#ifndef COEFS_H_
#define COEFS_H_

/* Largo de la planta, del correlador y del filtro adaptativo (consigna: 5 a 10 taps) */
#define COEFS_L 8U

/* Parte 1: respuesta al impulso de la planta a identificar (8 muestras,
 * valores arbitrarios fijos). Energía sum(p^2) = 0,974; suma = 1,005. */
extern const float g_planta[COEFS_L];

/* Parte 2: FIR "correlador" que relaciona el ruido que contamina la señal con
 * la referencia que ve el procesador adaptativo. c[k] = sqrt(1 - 0,49) 0,7^k:
 * energía sum(c^2) = 0,997 ~ 1, así que el ruido sale con la misma potencia
 * que entra. Es (prácticamente) de fase mínima, así que también tiene inversa
 * estable, lo que hace falta si se lo pone en la rama de la referencia
 * (ver parte2.h). */
extern const float g_correlador[COEFS_L];

#endif /* COEFS_H_ */
