# TP2 – Análisis de diseño del filtro FIR pasa altos

- **Consigna:** Pasa altos, 
- **Fc = 35 Hz**, **Astop = 30 dB**, 
- FIR procesado por muestras, 
- Para fs = 8, 16, 22, 44 y 48 kHz, en la FRDM-MCXN947 (Cortex-M33 a 150 MHz) con CMSIS-DSP en Q15.

Este documento reúne las formas de diseñar el filtro con la herramienta de MATLAB (`filterDesigner`, ex `fdatool`), las compara con números y termina en un veredicto con los parámetros exactos para cargar en la herramienta.

---

## 0. Resumen y veredicto

| | Recomendación |
|---|---|
| Interpretación de "Fc = 35 Hz" | **Borde de la banda de paso** (Fpass = 35 Hz): una señal de 35 Hz pasa casi intacta (−0,5 dB). |
| Método en `filterDesigner` | **FIR → Equiripple**, **Minimum order** |
| Especificaciones | **Fstop = 5 Hz, Fpass = 35 Hz, Apass = 1 dB, Astop = 31 dB** (30 dB + 1 dB de margen para la cuantización Q15) |
| Orden / taps resultantes | 8k: 260 (262 taps) · 16k: 518 (520) · 22k: ≈716 (≈718) · 44k: ≈1430 (≈1432) · 48k: 1560 (1562) |
| Comparado con el diseño actual del repo | El diseño actual (Kaiser, Fstop = 20 Hz) usa 822…4918 taps; el recomendado usa **≈68 % menos** en todas las fs. |
| Retardo de grupo | ≈16 ms en todas las fs (actual: ≈51 ms). |

**Lo que hay que saber antes de elegir:** con `arm_fir_q15` y `BLOCK_SIZE 1`, el costo estimado (ver §8) hace que **el filtro recomendado entre en tiempo real a 8 y 16 kHz, quede al límite a 22 kHz y no entre a 44/48 kHz**. Ningún FIR que salga de la herramienta y cumpla Fpass = 35 Hz entra a 44/48 kHz con `arm_fir_q15` por muestras en este micro. Para cubrir esas fs hay dos caminos (§9 y §10):
1. un FIR propio optimizado (simetría + buffer circular), que alcanza hasta 22 kHz con holgura y queda justo a 44/48 kHz, o
2. el FIR de **promedios móviles en cascada** (fuera de la herramienta), que cumple la especificación en las 5 fs con un costo de pocos ciclos por muestra.

Los números de ciclos son **estimaciones**; hay que medirlos en la placa (§11).

---

## 1. Qué dice la consigna y qué deja abierto

La consigna da solo **Fc** y **Astop**. Para diseñar un FIR de orden mínimo la herramienta pide además:

| Parámetro | ¿Lo da la consigna? | Qué decidimos |
|---|---|---|
| Fs | Sí (5 valores) | Un diseño por cada fs |
| Fpass (borde de banda de paso) | Indirectamente (Fc) | 35 Hz |
| Fstop (borde de banda de rechazo) | **No** | Decisión de diseño (§5): 5 Hz |
| Apass (ripple en banda de paso) | **No** | 1 dB (valor por defecto de la herramienta) |
| Astop | Sí: 30 dB | 31 dB (margen Q15, §7) |
| Método y orden | **No** | Equiripple, orden mínimo |

### 1.1 Tres maneras de leer "Fc = 35 Hz"

| Interpretación | Qué significa | Ganancia a 35 Hz | Orden a 8 kHz* |
|---|---|---|---|
| **A. Fc = Fpass** (recomendada) | A partir de 35 Hz la señal pasa con ≤ Apass de error | −0,5 dB | **260** (Fstop 5 Hz) |
| B. Fc = punto de −6 dB | Es lo que significa "Fc" en el método **Window** (`fir1`): Fc queda en el medio de la transición | ≈ −7 dB | 316 (Fstop 20 / Fpass 50) · 496 (25 / 45) |
| C. Fc = punto de −3 dB | Definición "clásica" de frecuencia de corte; en Equiripple equivale a usar Apass = 3 dB | −1,6 dB | 310 (Apass 3 dB, Fstop 10) |

\* Equiripple, Astop = 30 dB, valores calculados (ver §3).

La **A** es la más estricta (nadie puede objetar que 35 Hz "pase") y, combinada con un Fstop bajo, termina siendo **la más barata**. La B atenúa 7 dB justo en 35 Hz y no ahorra taps frente a A con Fstop = 5 Hz. La C obliga a tener 3 dB de ripple en **toda** la banda de paso (Equiripple reparte el error parejo hasta fs/2), lo cual es feo para audio.

> Ojo: en el informe conviene escribir explícitamente qué interpretación se usó y por qué; el pasa bajos del repo usó la B (3600 Hz a −6 dB) y el pasa altos actual usó la A.

---

## 2. Cómo se carga en `filterDesigner`

Abrir con `filterDesigner` (o `fdatool` en versiones viejas). Los paneles relevantes:

| Panel | Opción para el pasa altos |
|---|---|
| **Response Type** | Highpass |
| **Design Method** | FIR → Equiripple / Least-squares / Window / Constrained Least-squares / … (según la versión aparecen más) |
| **Filter Order** | *Minimum order* (la herramienta busca el menor orden que cumple) o *Specify order* (lo ponés vos) |
| **Options** | Depende del método: Equiripple → *Density factor*; Window → tipo de ventana, parámetro (β de Kaiser, atenuación de Chebyshev…) y *Scale passband* |
| **Frequency Specifications** | Units: Hz · Fs · **Fstop** · **Fpass** (con Window + orden especificado aparece **Fc** en lugar de Fstop/Fpass) |
| **Magnitude Specifications** | Units: dB · **Astop** · **Apass** |

Qué combinaciones existen:

- *Minimum order* está disponible para **Equiripple** y para **Window solo con Kaiser** (usa `kaiserord`). Least-squares, Constrained Least-squares y las demás ventanas requieren *Specify order*: el orden mínimo hay que encontrarlo a mano subiendo/bajando y mirando la respuesta.
- Un FIR pasa altos de fase lineal **tiene que ser de orden par** (tipo I: largo impar). Los de orden impar (tipo II) tienen un cero obligado en fs/2, justo donde el pasa altos tiene que pasar. La herramienta lo fuerza o lo avisa.
- Como `arm_fir_init_q15` exige una cantidad **par** de coeficientes (≥ 4), a los N+1 coeficientes (impar) se les agrega **un cero al final**. Por eso los taps en CMSIS son N+2. El generador de `filtros.h` ya hace eso.

---

## 3. Cómo se hizo este análisis (y sus límites)

No había MATLAB disponible, así que se implementaron los mismos algoritmos que usa la herramienta: Parks-McClellan/Remez (Equiripple), mínimos cuadrados (`firls`), método de ventanas (`fir1`) con Kaiser + `kaiserord`, Hamming, Hann, Blackman, Chebyshev, rectangular y Bartlett.

- **Cada diseño se verificó con su respuesta en frecuencia real**: máximo en la banda de rechazo [0, Fstop] y mínimo/máximo en la banda de paso [Fpass, fs/2], antes y después de cuantizar a Q15.
- **Validación contra el repo:** con Kaiser, Fstop = 20, Fpass = 35, Apass = 1, Astop = 30, `kaiserord` da exactamente orden 820 a 8 kHz y 4916 a 48 kHz, es decir **822 y 4918 taps, idénticos a `PA_8k` y `PA_48k` de `filtros.h`**. O sea, así se diseñó el pasa altos actual.
- **Límite:** a órdenes altas (22/44 kHz con algunas Fstop) la implementación de Remez no convergió siempre. Esos valores se marcan con "≈" y salen de la regla de escalado: con especificaciones fijas en Hz, **el orden crece proporcional a fs** (verificado: 260 a 8 kHz → 518 a 16 kHz → 1560 a 48 kHz = 260 × 6). En `filterDesigner` pueden salir unos pocos taps distintos.

---

## 4. Comparación de métodos (fs = 8 kHz)

Orden mínimo N que cumple Fpass = 35 Hz, Apass = 1 dB, Astop = 30 dB (verificado con la respuesta real). Taps en CMSIS = N + 2.

| Fstop (Δf) | Equiripple | Least-squares | Kaiser (`kaiserord`) | Kaiser (real) | Chebyshev 30 dB | Hamming | Hann | Blackman |
|---|---|---|---|---|---|---|---|---|
| 30 Hz (5 Hz) | **1910** | 3028 | 2458 ✗ | — | 3078 | 4032 | 4210 | 5338 |
| 25 Hz (10 Hz) | **910** | 1464 | 1230 ✗ | — | 1504 | 2024 | 2106 | 2670 |
| 20 Hz (15 Hz) | **648** | 994 | 820 ✓ | 804 | 1028 | 1352 | 1404 | 1780 |
| 15 Hz (20 Hz) | **456** | 766 | 616 ✗ | 774 | 790 | 1002 | 1054 | 1336 |
| 10 Hz (25 Hz) | **424** | 558 | 492 ✗ | 536 | 608 | 804 | 846 | 1068 |
| 5 Hz (30 Hz) | **260** | 352 | 410 ✗ | 604 | 512 | 666 | 682 | 892 |

- ✗ = el orden que propone `kaiserord` (lo que te da *Minimum order* con Kaiser) **no cumple** la especificación; hay que subirlo a mano. "Kaiser (real)" es el mínimo que cumple con el mismo β.
- **Rectangular y Bartlett no sirven:** no alcanzan 30 dB de atenuación (rectangular: −25,7 dB con N = 2000; Bartlett: −20,8 dB con N = 2000).
- Least-squares: se tomó el mejor de pesos (Wstop, Wpass) = (1, 1) y (10, 1).

### 4.1 Método por método

| Método | Cómo funciona | Para este filtro |
|---|---|---|
| **Equiripple** (Parks-McClellan) | Minimiza el error máximo: reparte el ripple parejo en cada banda. Es el óptimo para especificaciones "Apass/Astop" | **El de menor orden en todos los casos.** Permite orden mínimo automático. |
| **Least-squares** | Minimiza la energía del error. Deja picos de error cerca de los bordes de banda | 1,3–1,7× más taps que Equiripple. Requiere orden especificado. |
| **Window – Kaiser** | Ideal truncado × ventana de Kaiser con β ajustable a la atenuación (β = 2,12 para 30 dB) | 1,25–2,3× más taps. `kaiserord` subestima el orden cuando la banda de rechazo está pegada a 0 Hz, así que el diseño hay que verificarlo. Además la atenuación cerca de DC varía de forma errática con N. |
| **Window – Chebyshev** | Ventana con lóbulos laterales de altura constante | La mejor de las ventanas de orden fijo, pero ~1,4–2× Equiripple. |
| **Window – Hamming / Hann / Blackman** | Ventanas fijas (≈53 / 44 / 74 dB de lóbulos) | Sobredimensionadas: dan mucha más atenuación de la necesaria y pagan con 2–3,5× más taps. |
| **Window – Rectangular / Bartlett** | | No llegan a 30 dB: descartadas. |
| **Constrained Least-squares** | Mínimos cuadrados con tope de ripple; se especifica **Fc**, Apass y Astop y el orden | Es la que mapea literal la consigna ("Fc" + "Astop"), pero no tiene orden mínimo automático. No se calculó; se espera un orden entre Equiripple y Least-squares. |
| **Maximally flat** | Respuesta sin ripple | No permite fijar Astop y necesita órdenes enormes para transiciones tan angostas: no apto. |
| **Interpolated FIR (IFIR)** | Cascada de un filtro "estirado" (disperso) y otro que limpia las imágenes | No evaluado. Complica mucho el código (dos etapas, una dispersa) y la banda de paso del pasa altos es muy ancha, lo que juega en contra del método. |
| **Equiripple + Minimum phase** (opción) | Fase mínima en lugar de lineal | Menor retardo, pero se pierde la fase lineal y la simetría de coeficientes (que es lo que permite optimizar el cálculo, §9). No recomendado. |

---

## 5. La decisión que más pesa: Fstop (ancho de la transición)

El costo del filtro lo domina el **ancho de la banda de transición Δf = Fpass − Fstop**, no la atenuación. Equiripple, 8 kHz, Fpass = 35 Hz:

| Fstop | 30 | 25 | 20 | 15 | 10 | **5** |
|---|---|---|---|---|---|---|
| Orden | 1910 | 910 | 648 | 456 | 424 | **260** |

¿Qué significa Fstop = 5 Hz? Que garantizamos ≥ 30 dB de atenuación entre 0 y 5 Hz (la componente de continua del ADC y la deriva lenta, que es lo que un pasa altos de 35 Hz viene a eliminar), y que entre 5 y 35 Hz la atenuación va bajando gradualmente. La consigna no fija dónde empieza la banda de rechazo, así que es una decisión válida. Hay que justificarla en el informe.

Si la cátedra pide una banda de rechazo más "seria", **Fstop = 10 Hz** cuesta +65 % de taps (424 vs 260) y ya no entra a 16 kHz con `arm_fir_q15` (§8).

### 5.1 Efecto de Apass y Astop (8 kHz, Fstop = 10 Hz)

| Apass | 0,1 dB | 0,5 dB | **1 dB** | 2 dB | 3 dB |
|---|---|---|---|---|---|
| Orden | 634 | 490 | **424** | 354 | 310 |

| Astop | **30 dB** | 31 dB | 33 dB | 40 dB |
|---|---|---|---|---|
| Orden | **424** | 428 | 434 | 444 |

Como la banda de rechazo está pegada a 0 Hz y es angosta, **subir Astop es casi gratis** (+5 % por 10 dB más), mientras que bajar Apass sí encarece. Por eso el margen para la cuantización conviene ponerlo en Astop.

---

## 6. Todas las frecuencias de muestreo

Orden N (taps en CMSIS = N + 2). Fpass = 35 Hz, Apass = 1 dB.

| Diseño | 8 kHz | 16 kHz | 22 kHz | 44 kHz | 48 kHz |
|---|---|---|---|---|---|
| **Equiripple, Fstop 5, Astop 31 (recomendado)** | **260** | **518** | **≈716** | **≈1430** | **1560** |
| Equiripple, Fstop 10, Astop 31 | 428 | 854 | 1174 | 2358 | 2570 |
| Equiripple, Fstop 20, Astop 31 | 652 | 1302 | 1792 | 3582 | 3912 |
| Kaiser real, Fstop 10, Astop 30 | 536 | 1072 | 1474 | 2948 | 3216 |
| Kaiser `kaiserord`, Fstop 20 (**diseño actual del repo**) | 820 | 1640 | 2254 | 4506 | 4916 |
| Promedio móvil² (§10), Fstop ≈ 4,3 Hz | 362 | 726 | 1000 | 2000 | 2180 |

Retardo de grupo = N / (2·fs): el recomendado tiene **≈16 ms** en todas las fs; el actual ≈51 ms; el de promedio móvil ≈23 ms.

---

## 7. Cuantización a Q15

Al pasar los coeficientes a Q15 (`round(h·32768)`):

- Los diseños hechos **justo** con Astop = 30 dB quedan apenas por fuera después de cuantizar (por ejemplo, −29,7 dB a 44/48 kHz con Fstop 20). Con **Astop = 31 dB** todos los diseños recomendados quedan entre −31 y −34 dB después de cuantizar.
- El ripple de banda de paso se corre unas centésimas de dB (0,49 contra un límite de 0,486 dB); en la práctica no importa. Si se quiere cumplir al pie de la letra, usar **Apass = 0,9 dB**: cuesta +3 % de taps (268 a 8 kHz, 534 a 16 kHz) y después de cuantizar queda dentro de 1 dB y por debajo de −31 dB (verificado).
- A 44/48 kHz con transiciones angostas muchos coeficientes valen menos de ½ LSB y se redondean a 0 (por ejemplo, 358 coeficientes en el Equiripple de Fstop 20 a 48 kHz). Es una razón más para preferir transiciones anchas.
- El coeficiente central vale ≈ 0,99 (≈ 32 540…32 740), sin riesgo de saturación.

En `filterDesigner` esto se puede ver antes de exportar: **Set quantization parameters** → *Filter arithmetic: Fixed-point* → numerador de 16 bits con 15 bits fraccionarios. La herramienta superpone la respuesta cuantizada.

---

## 8. ¿Entra en tiempo real? (costo por muestra)

### 8.1 Cuánto cuesta `arm_fir_q15` con `BLOCK_SIZE 1`

Con bloques de 1 muestra, `arm_fir_q15` (CMSIS del repo, `arm_fir_q15.c`) no usa el lazo SIMD sino el escalar: **una multiplicación-acumulación por coeficiente** (`acc0 += *px++ * *pb++`). Después **copia los N−1 valores del estado** una posición, en cada muestra. En el proyecto **no** está definido `ARM_MATH_LOOPUNROLL`, así que esa copia también es de a una muestra.

Estimación (no medida): **≈ 9 ciclos por tap** compilando con `-O2/-O3` (≈4–5 del MAC + ≈4–5 de la copia) más ≈200 ciclos fijos de la ISR. En **Debug** el proyecto no tiene nivel de optimización configurado (`-O0`, por defecto de MCUXpresso), así que es **varias veces peor**.

> Corrección: en una respuesta anterior estimé 1,5–2 ciclos por tap. Mirando el código real, esa cifra era optimista.

Ciclos disponibles por muestra a 150 MHz y taps máximos usando el 80 %:

| fs | Ciclos por muestra | Máx. taps `arm_fir_q15` (≈9 c/tap) | Máx. taps FIR propio optimizado (≈2 c/tap) |
|---|---|---|---|
| 8 kHz | 18 750 | ≈1640 | ≈7400 |
| 16 kHz | 9 375 | ≈810 | ≈3650 |
| 22 kHz | 6 818 | ≈580 | ≈2620 |
| 44 kHz | 3 409 | ≈280 | ≈1260 |
| 48 kHz | 3 125 | ≈255 | ≈1150 |

### 8.2 Carga estimada de cada opción

| Opción | 8k | 16k | 22k | 44k | 48k |
|---|---|---|---|---|---|
| Actual del repo (Kaiser F20) + `arm_fir_q15` | 41 % | **160 %** ✗ | 300 % ✗ | 1200 % ✗ | 1420 % ✗ |
| Equiripple F10 + `arm_fir_q15` | 22 % | 84 % ⚠ | 158 % ✗ | 630 % ✗ | 750 % ✗ |
| **Equiripple F5 + `arm_fir_q15`** | **14 %** | **52 %** | 98 % ⚠ | 384 % ✗ | 456 % ✗ |
| Equiripple F5 + FIR propio optimizado | 4 % | 13 % | 24 % | ≈90 % ⚠ | ≈106 % ✗ |
| Promedio móvil² recursivo (§10) | <1 % | <1 % | ≈1 % | ≈2 % | ≈2 % |

Cuando la carga pasa el 100 %, la ISR del ADC tarda más que el período de muestreo: se pierden conversiones, la fs real baja y los botones pueden dejar de responder. **El pasa altos actual del repo probablemente ya no entra a 16 kHz.**

---

## 9. Opción: FIR propio optimizado (sigue siendo el mismo filtro)

Los mismos coeficientes de la herramienta, pero calculados sin desperdicio:

1. **Sin copiar el estado:** buffer circular de largo 2·L donde cada muestra se escribe dos veces (en `i` y en `i+L`), así siempre hay una ventana contigua y no hay que mover nada.
2. **Simetría:** como h[k] = h[N−k], se suman primero las dos muestras que comparten coeficiente y se multiplica una sola vez, con la mitad de multiplicaciones.
3. **SIMD del M33:** `__SHADD16` (suma de a dos muestras de 16 bits) + `__SMLALD` (dos MAC por instrucción).

Estimado ≈ 2 ciclos por tap original o menos. Cubre 8/16/22 kHz con mucha holgura y queda **justo** a 44/48 kHz. Requiere escribir y probar esa función, y no es `arm_fir_q15`.

---

## 10. Opción fuera de la herramienta: promedios móviles en cascada

Un pasa altos FIR también se puede armar como **"la señal retrasada menos un pasa bajos"**, usando como pasa bajos dos **promedios móviles** de L muestras en cascada:

  h[n] = δ[n − (L−1)] − (1/L²) · (rect_L ∗ rect_L)[n]  (FIR de fase lineal, 2L−1 taps)

Con L elegido para que 35 Hz pase a −0,5 dB:

| fs | L | Taps equivalentes | Atenuación ≥ 30 dB hasta | Ganancia a 35 Hz | En DC |
|---|---|---|---|---|---|
| 8k | 182 | 363 | 4,3 Hz | −0,51 dB | −∞ (cero exacto) |
| 16k | 364 | 727 | 4,3 Hz | −0,51 dB | −∞ |
| 22k | 501 | 1001 | 4,3 Hz | −0,51 dB | −∞ |
| 44k | 1001 | 2001 | 4,3 Hz | −0,51 dB | −∞ |
| 48k | 1091 | 2181 | 4,3 Hz | −0,51 dB | −∞ |

Cumple Fpass = 35 Hz con Apass ≤ 1 dB y Astop ≥ 30 dB (verificado con la respuesta real). Lo interesante es que **no se calcula como convolución**. Se implementa con sumas acumuladas en enteros (cada promedio móvil es `s += x[n] − x[n−L]`), que son exactas y cuestan lo mismo para cualquier L:

```c
/* por muestra (x en Q15, s1 int32, s2 int64) */
s1 += x - x_hist[(n - L) mod L];           /* primer promedio (sin dividir) */
s2 += s1 - s1_hist[(n - L) mod L];          /* segundo promedio              */
y   = x_retrasado_L_menos_1 - (s2 / (L*L)); /* pasa altos = retardo − pasa bajos */
```

| A favor | En contra |
|---|---|
| Cumple la especificación en las **5 fs** con ≈1–2 % de CPU | No sale de `filterDesigner` (se puede importar `h` a la herramienta para mostrar su respuesta: *File → Import Filter from Workspace*) |
| Sin error de cuantización de coeficientes (aritmética entera exacta) | No usa `arm_fir_q15`; **conviene consultar con la cátedra** si se acepta como "FIR por muestras" |
| Anula exactamente la continua | La banda de rechazo efectiva es 0–4,3 Hz (igual que la opción recomendada) |
| Es el "Moving Average" de las diapositivas del curso | Con coeficientes directos en Q15 no sirve: 1/L² es menor que 1 LSB para L > 181 |

---

## 11. Cómo verificarlo en la placa

1. **Compilar optimizado:** en *Properties → C/C++ Build → Settings → MCU C Compiler → Optimization* usar `-O2` u `-O3`, o compilar en Release. Opcional: agregar el define `ARM_MATH_LOOPUNROLL` (acelera la copia del estado).
2. **Medir ciclos** con el contador del core (DWT) dentro de la ISR del ADC:

   ```c
   /* una vez, en main() */
   DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
   DWT->CYCCNT = 0;
   DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

   /* en la ISR */
   uint32_t t0 = DWT->CYCCNT;
   arm_fir_q15(pte_aux, &buffer_in[i], &buffer_out[i], BLOCK_SIZE);
   uint32_t c = DWT->CYCCNT - t0;
   if (c > ciclos_max) ciclos_max = c;   /* volatile global, se mira en el debugger */
   ```

   Comparar `ciclos_max` con 150 000 000 / fs.
3. **Con el osciloscopio:** poner un GPIO en alto al entrar a la ISR y en bajo al salir. El ancho del pulso tiene que ser bastante menor que el período 1/fs.
4. **Respuesta en frecuencia:** con el generador, barrer 1, 5, 10, 20, 35, 50, 100 Hz y 1 kHz y medir la amplitud de salida contra BYPASS. Se espera ≤ −30 dB por debajo de 5 Hz y ≈ −0,5 dB desde 35 Hz.

---

## 12. Abanico de opciones

| # | Opción | Taps 8k / 48k | ¿Cumple la consigna? | Tiempo real con `arm_fir_q15` | Complejidad | Comentario |
|---|---|---|---|---|---|---|
| 1 | **Equiripple, Fstop 5, Astop 31** | 262 / 1562 | Sí (lectura A) | 8k ✓ 16k ✓ 22k ⚠ 44/48k ✗ | Baja: todo en la herramienta | **Recomendada** |
| 2 | Equiripple, Fstop 10, Astop 31 | 430 / 2572 | Sí, con banda de rechazo más ancha | 8k ✓ 16k ⚠ resto ✗ | Baja | Si piden Fstop "razonable" |
| 3 | Equiripple, Fstop 20 | 654 / 3914 | Sí | 8k ✓ resto ✗ | Baja | Transición angosta, cara |
| 4 | Kaiser (diseño actual) | 822 / 4918 | Sí | 8k ✓ resto ✗ | Baja | Es lo que está hoy en `filtros.h` |
| 5 | Kaiser con orden mínimo y Fstop < 20 | | **No siempre**: `kaiserord` subestima | | Baja | Requiere verificar a mano |
| 6 | Least-squares / Chebyshev / Hamming… | 1,4–4× la opción 1 | Sí | Peor que la 1 | Media (orden a mano) | Sin ventajas para esta especificación |
| 7 | Lectura B (Fc a −6 dB, Fstop 20 / Fpass 50) | 318 / 1988 | Discutible: 35 Hz sale a −7 dB | 8k ✓ 16k ⚠ resto ✗ | Baja | Más taps que la 1 |
| 8 | Opción 1 + FIR propio optimizado | 262 / 1562 | Sí | 8/16/22k ✓, 44k ⚠, 48k ✗ (estimado) | Alta | Mejora real si se mide |
| 9 | Promedio móvil² recursivo | (363 / 2181 equiv.) | Sí (Fstop 4,3 Hz) | Las 5 fs ✓ (≈2 %) | Media | Fuera de la herramienta, consultar |
| 10 | Rectangular / Bartlett | — | **No** (no llegan a 30 dB) | — | — | Descartadas |

## 13. Veredicto

1. **Diseño en la herramienta (lo que pide la consigna): opción 1.** `filterDesigner` → Highpass → FIR **Equiripple** → **Minimum order** → Units Hz, **Fs** = cada una, **Fstop = 5**, **Fpass = 35** → Units dB, **Astop = 31**, **Apass = 1**. Repetir para las 5 fs. Es el método de menor orden en todos los casos medidos: entre 20 % y 57 % menos taps que Kaiser con iguales especificaciones, y −68 % frente al diseño actual. Cumple la especificación con Q15 y tiene el menor retardo.
2. **Implementación:** con `arm_fir_q15` por muestras alcanza (estimado) a **8 y 16 kHz**; a 22 kHz queda al límite. Para 44/48 kHz ningún FIR de la herramienta que respete Fpass = 35 Hz entra en este micro con `arm_fir_q15`. Ahí hay dos salidas: la opción 9 (si la cátedra la acepta) o documentar la limitación con la medición de ciclos, que es una conclusión válida del TP y lleva naturalmente a los IIR.
3. **Medir antes de decidir:** los porcentajes de CPU son estimaciones. Con el contador DWT (§11) se confirma en minutos, en la placa, qué combinaciones entran.

### Pasos en `filterDesigner` para la opción 1

1. Response Type: **Highpass**. Design Method: **FIR → Equiripple**. Filter Order: **Minimum order**.
2. Frequency Specifications: Units **Hz**, Fs **8000**, Fstop **5**, Fpass **35**.
3. Magnitude Specifications: Units **dB**, Astop **31**, Apass **1**. → **Design Filter**. Anotar el orden (≈260).
4. **Set quantization parameters** → Fixed-point, numerador 16 bits / 15 fraccionarios → verificar que la respuesta cuantizada siga por debajo de −30 dB entre 0 y 5 Hz.
5. Exportar (*File → Export* al workspace como `Num`, o *Targets → Generate C Header* como entero de 16 bits) y regenerar `filtros.h` con el script, que invierte, rellena con un cero si hace falta y arma las tablas.
6. Repetir con Fs = 16000, 22000, 44000 y 48000.
