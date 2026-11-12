/*
 * TP4 - Procesamiento Digital de Señales
 * txt_out.c: armado de líneas de texto CSV (ver txt_out.h).
 */
#include "txt_out.h"

#include <string.h>

static void put(txt_line_t *l, const char *s, size_t n)
{
    if ((l->len + n) >= (sizeof(l->buf) - 3U)) /* deja lugar para "\r\n\0" */
    {
        n = sizeof(l->buf) - 3U - l->len;
    }
    (void)memcpy(&l->buf[l->len], s, n);
    l->len += n;
}

static size_t fmt_u32(char *s, uint32_t v)
{
    char tmp[10];
    size_t n = 0U;
    do
    {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    for (size_t i = 0U; i < n; i++)
    {
        s[i] = tmp[n - 1U - i];
    }
    return n;
}

size_t txt_fmt_f32(char *s, float v)
{
    size_t n = 0U;

    if (v != v) /* NaN: el filtro divergió */
    {
        (void)memcpy(s, "nan", 3U);
        return 3U;
    }
    if (v < 0.0f)
    {
        s[n++] = '-';
        v      = -v;
    }
    if (v > 3.0e38f)
    {
        (void)memcpy(&s[n], "inf", 3U);
        return n + 3U;
    }
    if (v == 0.0f)
    {
        (void)memcpy(&s[n], "0.0000e+00", 10U);
        return n + 10U;
    }

    /* Normaliza a 1 <= v < 10 y redondea a 5 cifras significativas */
    int exp10 = 0;
    while (v >= 10.0f)
    {
        v /= 10.0f;
        exp10++;
    }
    while (v < 1.0f)
    {
        v *= 10.0f;
        exp10--;
    }
    uint32_t m = (uint32_t)(v * 10000.0f + 0.5f); /* 10000 .. 100000 */
    if (m >= 100000U)
    {
        m /= 10U;
        exp10++;
    }

    s[n++] = (char)('0' + (m / 10000U));
    s[n++] = '.';
    s[n++] = (char)('0' + ((m / 1000U) % 10U));
    s[n++] = (char)('0' + ((m / 100U) % 10U));
    s[n++] = (char)('0' + ((m / 10U) % 10U));
    s[n++] = (char)('0' + (m % 10U));
    s[n++] = 'e';
    if (exp10 < 0)
    {
        s[n++] = '-';
        exp10  = -exp10;
    }
    else
    {
        s[n++] = '+';
    }
    s[n++] = (char)('0' + ((exp10 / 10) % 10));
    s[n++] = (char)('0' + (exp10 % 10));
    return n;
}

void txt_begin(txt_line_t *l, const char *tag)
{
    l->len = 0U;
    put(l, tag, strlen(tag));
}

void txt_str(txt_line_t *l, const char *s)
{
    put(l, ",", 1U);
    put(l, s, strlen(s));
}

void txt_u32(txt_line_t *l, uint32_t v)
{
    char tmp[12];
    put(l, ",", 1U);
    put(l, tmp, fmt_u32(tmp, v));
}

void txt_f32(txt_line_t *l, float v)
{
    char tmp[16];
    put(l, ",", 1U);
    put(l, tmp, txt_fmt_f32(tmp, v));
}

static void key(txt_line_t *l, const char *k)
{
    put(l, ",", 1U);
    put(l, k, strlen(k));
    put(l, "=", 1U);
}

void txt_kv_str(txt_line_t *l, const char *k, const char *v)
{
    key(l, k);
    put(l, v, strlen(v));
}

void txt_kv_u32(txt_line_t *l, const char *k, uint32_t v)
{
    char tmp[12];
    key(l, k);
    put(l, tmp, fmt_u32(tmp, v));
}

void txt_kv_f32(txt_line_t *l, const char *k, float v)
{
    char tmp[16];
    key(l, k);
    put(l, tmp, txt_fmt_f32(tmp, v));
}

void txt_end(txt_line_t *l, txt_write_fn wr)
{
    l->buf[l->len++] = '\r';
    l->buf[l->len++] = '\n';
    l->buf[l->len]   = '\0';
    wr(l->buf, l->len);
}
