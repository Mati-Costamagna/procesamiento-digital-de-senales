/*
 * TP4 - Procesamiento Digital de Señales
 * txt_out.h: armado de líneas de texto CSV sin usar printf("%f").
 *
 * La newlib-nano / redlib de MCUXpresso por defecto no imprime floats con
 * printf (hay que habilitarlo en las opciones del proyecto y suma ~10 KB).
 * Para no depender de eso, los floats se formatean a mano en notación
 * científica con 5 cifras significativas (ej. -1.2345e-03), que es lo que
 * leen sin problema MATLAB (str2double / sscanf) y cualquier terminal.
 */
#ifndef TXT_OUT_H_
#define TXT_OUT_H_

#include <stddef.h>
#include <stdint.h>

typedef void (*txt_write_fn)(const char *s, size_t len);

typedef struct
{
    char buf[512];
    size_t len;
} txt_line_t;

void txt_begin(txt_line_t *l, const char *tag);   /* empieza la línea con "TAG" */
void txt_str(txt_line_t *l, const char *s);       /* agrega ",s" */
void txt_u32(txt_line_t *l, uint32_t v);          /* agrega ",v" */
void txt_f32(txt_line_t *l, float v);             /* agrega ",d.dddde±xx" */
void txt_kv_str(txt_line_t *l, const char *k, const char *v); /* agrega ",k=v" */
void txt_kv_u32(txt_line_t *l, const char *k, uint32_t v);
void txt_kv_f32(txt_line_t *l, const char *k, float v);
void txt_end(txt_line_t *l, txt_write_fn wr);     /* agrega "\r\n" y la manda */

/* Formatea un float en s (al menos 16 bytes). Devuelve la cantidad de caracteres. */
size_t txt_fmt_f32(char *s, float v);

#endif /* TXT_OUT_H_ */
