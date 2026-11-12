/*
 * TP4 - Procesamiento Digital de Señales
 * coefs.h: coeficientes FIJOS de la planta (parte 1) y del correlador (parte 2).
 */
#ifndef COEFS_H_
#define COEFS_H_

#define COEFS_L 30U

/* Parte 1: respuesta al impulso de la planta a identificar (30 muestras,
 * valores arbitrarios fijos). Energía sum(p^2) = 1,979. */
extern const float g_planta[COEFS_L];

/* Parte 2: FIR "correlador" que relaciona el ruido que contamina la señal con
 * la referencia que ve el procesador adaptativo. c[k] = 0,6 * 0,8^k:
 * energía sum(c^2) = 1,000, así que el ruido sale con la misma potencia que
 * entra. Es de fase mínima (sus ceros están en |z| = 0,8), así que también
 * tiene inversa estable, lo que hace falta si se lo pone en la rama de la
 * referencia (ver parte2.h). */
extern const float g_correlador[COEFS_L];

#endif /* COEFS_H_ */
