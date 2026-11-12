# TP4 – Filtros adaptativos LMS en la FRDM-MCXN947

Laboratorio 4 de **Procesamiento Digital de Señales** (FCEFyN – UNC).

> **Consigna.** Parte 1: mostrar el proceso adaptativo de un filtro transversal de 30 coeficientes con LMS, en el esquema de *identificación de planta*, con distintos valores de μ, entrada aleatoria y entrada constante, calculando el MSE cada 100 muestras y la diferencia entre los coeficientes de la planta y los del filtro. Parte 2: usar un FIR de 30 coeficientes para correlacionar un ruido aleatorio con la referencia del procesador adaptativo, sumarle ese ruido a una senoidal que entra a la placa y demostrar que el filtro converge al óptimo que cancela el ruido, mostrando la señal en el osciloscopio.

```
PARTE 1 (todo adentro de la placa)              PARTE 2
                                                generador ─► ADC ─► s ─►(+)─► d ─►(Σ)─► e ─► DAC ─► osciloscopio
x (random/constante) ─┬─► PLANTA P(z) ─► d       random r ─► CORRELADOR C(z) ─► n' ─┘      ▲ -
                      │                  │(+)    r ─────────► FIR ADAPTATIVO W(z) ─► y ────┘
                      └─► FIR ADAPTATIVO ─► y(-) ─► e        (LMS: W converge a C, e ≈ s)
                           W(z), LMS
```

## Contenido del repo

| Carpeta | Qué hay |
|---|---|
| `TP4/source/` | Firmware: `main.c`, `lms.c/.h` (LMS de clase, `arm_lms_f32`, `arm_lms_norm_f32`), `parte1.c/.h`, `parte2.c/.h`, `coefs.c/.h`, `fir_fijo.h`, `prng.h`, `txt_out.c/.h`, `app_pins.c/.h`, `app_config.h` |
| `TP4/matlab/` | `tp4_experimentos.m` (corre todos los experimentos del informe en la placa), `tp4_captura.m`, `tp4_parse_log.m`, `tp4_graficos_p1.m`, `tp4_graficos_p2.m` |
| `TP4/tools/host_sim/` | El mismo código C compilado en la PC (`p1_sim`, `p2_sim`) y `figuras_informe.py`, que genera las figuras simuladas del informe |
| `TP4/tools/arm_build/` | Makefile para compilar el firmware fuera del IDE (sólo para verificar) |
| `documentacion/TP4/` | Informe en LaTeX: `main.tex`, `img/`, `src/` |

---

## Guía paso a paso

### Paso 1 – Crear el proyecto en MCUXpresso

1. Abrir **MCUXpresso IDE** con el workspace `C:\PDS\workspace`.
2. *File → New → Create a new C/C++ Project*.
3. Elegir la placa **frdmmcxn947** → *Next*.
4. En la pantalla de configuración:
   - **Project name:** `TP4_LMS`.
   - **Core:** `cm33_core0`.
   - **Project type:** C Project. **SDK Debug Console:** UART (da igual, no se usa).
   - En **Drivers** tildar: `clock`, `common`, `ctimer`, `gpio`, `lpadc`, `lpflexcomm`, `lpflexcomm_lpuart`, `lpdac`, `mcx_spc`, `port`, `reset` y `vref`.
   - En **CMSIS Include** / **Other** buscar y tildar la **CMSIS DSP Library** (igual que en el TP2 y el TP3).
5. *Finish*.
6. Si no apareció CMSIS-DSP: clic derecho sobre el proyecto → *SDK Management → Manage SDK Components* → pestaña *CMSIS* → tildar **DSP Library** → *OK* → aceptar los cambios.

### Paso 2 – Copiar el código

1. En el explorador de Windows abrir la carpeta `source` del proyecto (`C:\PDS\workspace\TP4_LMS\source`).
2. **Borrar** el `.c` que generó el asistente con el `main()` (se llama `TP4_LMS.c` o parecido). Dejar los demás archivos (`semihost_hardfault.c`, etc.).
3. Copiar **todos** los archivos de `TP4/source/` del repo a esa carpeta.
4. En MCUXpresso: clic derecho sobre el proyecto → *Refresh* (F5).

> No hace falta tocar el `pin_mux.c`: los pines los configura `APP_InitPins()` en `app_pins.c`. El reloj queda a 150 MHz con el `BOARD_InitBootClocks()` que genera el IDE.

### Paso 3 – Compilar

1. Conviene compilar con optimización para que los ciclos medidos sean representativos: clic derecho al proyecto → *Properties → C/C++ Build → Settings → MCU C Compiler → Optimization* → **-O2** (o usar la configuración *Release*).
2. Martillo (*Build*). Tiene que terminar sin errores. Si da `undefined reference to arm_lms_f32`, falta la biblioteca CMSIS-DSP (volver al paso 1.6).

### Paso 4 – Grabar y correr

1. Conectar la placa por el USB del **MCU-Link (J17)**.
2. *Debug* (el bichito). Si pregunta por J-Link, contestar **No**; elegir la sonda MCU-Link (SWD, dispositivo 0).
3. Cuando para en `main`, *Resume* (F8). O directamente cortar el debug: el programa queda grabado y arranca solo al resetear.
4. Abrir una terminal serie en **COM3**, **115200 8N1**: PuTTY, Tera Term, o en MCUXpresso el ícono *Open a Terminal* → *Serial Terminal*.
5. Apretar **RESET** en la placa. Aparece la ayuda con los comandos y una línea `#CFG` con la configuración. LED verde = Parte 1.

### Paso 5 – Parte 1 desde la terminal

Escribir cada comando y Enter (la terminal tiene que mandar Enter como CR o LF):

| Qué querés ver | Comandos |
|---|---|
| Una corrida con μ = 0,01, entrada aleatoria | `U0.01` → `S` |
| Barrido de μ (0,002 / 0,01 / 0,05 / 0,1 / 0,19 / 0,21) | `B` |
| Entrada constante | `E1` → `B` |
| Volver a entrada aleatoria | `E0` |
| Otra amplitud de entrada (potencia) | `A2` → `S` |
| NLMS | `G2` → `U0.3` → `S` |
| LMS escrito como en la clase | `G0` → `S` |
| Ruido de medición σᵥ = 0,01 | `V0.01` → `B` |
| No mandar las 30 diferencias (más rápido) | `D0` |
| Cortar la corrida | `X` o SW2 |

Con las teclas: **SW3 corto** pasa al siguiente μ de la lista y **SW2** arranca.

Qué sale por la terminal:

```
P1I,alg=CMSIS_LMS,mu=1.0000e-02,in=RANDOM,A=1.0000e+00,Px=3.3333e-01,...,mu_max=2.0000e-01,tau=1.5000e+02
P1P,6.9720e-01,7.0250e-01,...            ← coeficientes de la planta
P1M,100,4.2174e-01,1.1709e+00,-3.7496e+00 ← n, MSE del bloque, ||p-w||², MSE en dB
P1D,100,5.2e-01,...                      ← p_k - w_k de los 30 coeficientes
...
P1W,6.9665e-01,...                        ← coeficientes aprendidos
P1F,20000,3.2010e-06,4.0722e-06,412,OK    ← ciclos por muestra del LMS y estado
```

### Paso 6 – Parte 1 con MATLAB (para el informe)

1. **Cerrar la terminal** (MATLAB no puede abrir el COM si otro programa lo tiene abierto).
2. En MATLAB, ir a la carpeta `TP4/matlab`.
3. Abrir `tp4_experimentos.m`, revisar que diga `PUERTO = 'COM3';`.
4. Correr **sólo las secciones de la Parte 1** (E1 a E5): poner el cursor en la sección y **Ctrl+Enter**. Cada sección tarda entre 10 s y 1 min y muestra la curva en vivo.
5. Quedan los registros en `registros/` y las figuras en `img/`.
6. Para el informe: copiar `img/p1_e1_random_mse.png` y `img/p1_e2_const_coef.png` a `documentacion/TP4/img/placa/` (el informe las incluye solas). Anotar los ciclos que imprime la sección E4 en la tabla de ciclos del informe.
7. Sacar una captura de pantalla de la terminal durante una corrida y guardarla como `documentacion/TP4/img/placa/terminal_p1.png` (“mostrar el programa corriendo”).

### Paso 7 – Parte 2: conexiones

| Señal | Pin | Conector |
|---|---|---|
| Entrada del generador (ADC0_A2) | P4_23 | **J8 pin 28** |
| Salida DAC0 → osciloscopio CH1 | P4_2 | **J1 pin 4** |
| TRIG (pulso al reiniciar W) → disparo externo | P1_23 | **J1 pin 8** |
| PROC (en alto mientras procesa) → CH2 opcional | P4_0 | **J2 pin 18** |
| Masa | – | cualquier **GND** |

**Generador:** senoidal **200 Hz, 1 Vpp, offset 1,65 V**, salida en **High-Z**.
⚠️ Antes de conectarlo a la placa, mirarlo con el osciloscopio en acople DC: tiene que quedar entre ~1,15 V y ~2,15 V, **nunca por encima de 3,3 V ni por debajo de 0 V**. Unir las masas del generador, el osciloscopio y la placa.

### Paso 8 – Parte 2: pasar al cancelador y mirar el osciloscopio

1. En la terminal mandar `2` (o mantener **SW3 más de 0,7 s**). LED rojo = Parte 2. Empiezan a salir líneas `P2M` 80 veces por segundo (`M0` las apaga, `M10` manda 1 de cada 10).
2. **Osciloscopio:** CH1 en J1-4, acople DC, 200 mV/div, base de tiempo 2 ms/div, centrar en 1,65 V.
3. Elegir qué sale por el DAC con **SW3 corto** o con `O<n>`:
   - `O0` → **d** = señal + ruido (la senoidal casi no se ve).
   - `O1` → **e** = salida del cancelador (la senoidal limpia).
   - `O2` → **y** = ruido estimado. `O3` → **s** = senoidal sola. `O4` → residuo ×4.
4. Captura 1: `O0`, foto de la pantalla → `img/placa/osc_d.png`.
5. Captura 2: `O1`, esperar un par de segundos → `img/placa/osc_e.png`.
6. **Ver el aprendizaje:** `O1`, base de tiempo 100 ms/div, disparo **externo** con el cable de TRIG (J1-8), flanco ascendente, modo **Single**. Apretar **SW2** (pone los coeficientes en cero): la pantalla muestra la senoidal ruidosa que se va limpiando en ~1 s. Foto → `img/placa/osc_convergencia.png`. Para que sea más lento y se vea mejor: `U0.001` (τ ≈ 1,2 s, base de tiempo 500 ms/div).
7. Comandos útiles: `U<mu>` cambia μ sin borrar lo aprendido, `Z` = SW2, `C` imprime los coeficientes y los óptimos, `T` imprime el tiempo de cómputo, `Q1` pone el correlador en la rama de la referencia (como la figura 2 de la consigna), `Q0` lo vuelve al camino del ruido, `A<amp>` cambia la amplitud del ruido.
8. Mandar `T` y anotar la línea `P2T` en el informe (ciclos por muestra de toda la interrupción). Opcional: CH2 en PROC (J2-18) para ver el ancho del pulso contra el período de 125 µs.

### Paso 9 – Parte 2 con MATLAB

1. Cerrar la terminal. Con el generador conectado, correr las secciones **E6 y E7** de `tp4_experimentos.m`.
2. Copiar `img/p2_e6_mu_red.png` a `documentacion/TP4/img/placa/`.

### Paso 10 – Informe

`documentacion/TP4/main.tex` ya tiene la teoría, el diseño, los diagramas, los resultados simulados y el código. Faltan:

- los integrantes (`\Integrantes`, arriba de todo) y los modelos de generador y osciloscopio (tabla de equipamiento);
- las capturas de la placa en `img/placa/` con los nombres de arriba (mientras no estén, el PDF muestra un recuadro que dice qué falta);
- la tabla de ciclos y el tiempo de la línea `P2T`.

Se compila con `pdflatex main.tex` (dos veces) o subiendo la carpeta a Overleaf.

---

## Detalles de implementación

- **LMS.** `lms.c` tiene la rutina de la diapositiva *Implementación en DSP* (`LMS_ALG_CLASE`), `arm_lms_f32` y `arm_lms_norm_f32`, todas con `blockSize = 1`. Las tres usan `w += μ·e·x` (la convención de CMSIS), que equivale a `2μ` de la teoría: la condición de convergencia queda **0 < μ < 2/(L·Px)**. CMSIS guarda los coeficientes invertidos en el tiempo; `lms_get_w()` los da vuelta.
- **Planta** (`coefs.c`): pₖ = 0,8·e^(−k/8)·sin(0,3π(k+1)) + 0,05·cos(0,9k), 30 coeficientes. **Correlador:** cₖ = 0,6·0,8ᵏ (energía 1, fase mínima).
- **Ruido:** xorshift32 propio, uniforme en [−A, A) (Px = A²/3), idéntico en la placa y en la PC.
- **Parte 2:** CTIMER0 a 8 kHz dispara el ADC; la ISR del ADC resta la continua (seguimiento con α = 1/4096), genera el ruido, lo pasa por el correlador, corre el LMS y escribe el DAC. Como la señal limpia `s` se conoce, se mide el ruido residual `e − s` exacto.
- **Salida serie:** texto CSV con floats formateados a mano (`txt_out.c`), para no depender de que `printf` tenga habilitados los floats.

## Valores teóricos útiles

| Caso | μ_max | τ del MSE |
|---|---|---|
| Aleatoria, A = 1 (Px = 1/3) | 0,2 | 1,5/μ muestras (μ = 0,01 → 150) |
| Constante, A = 1 | 0,0667 | 1/(2μ·30) muestras |
| NLMS | 2 | 15/μ muestras |
| Cancelador, ruido 0,4 V (Px = 0,0533) | 1,25 | 9,4/μ muestras (μ = 0,005 → 0,23 s a 8 kHz) |

## Verificación hecha

- El firmware compila sin warnings propios con `arm-none-eabi-gcc` contra el MCUXpresso SDK 26.09 y CMSIS-DSP 1.16.2 (`TP4/tools/arm_build`): ~45 KB de flash y ~7 KB de RAM.
- `parte1.c` y `parte2.c` corridos en la PC con la misma CMSIS-DSP (`TP4/tools/host_sim`): la rutina de clase y `arm_lms_f32` dan resultados idénticos bit a bit; la divergencia aparece en μ = 0,2 (aleatoria) y entre 0,06 y 0,07 (constante), como predice la teoría; el desajuste con ruido de medición coincide con M = (μ·trR/2)/(1 − μ·trR/2).
- Los scripts de MATLAB (parser y gráficos) se probaron en Octave con los registros simulados.
- **Falta probarlo en la placa**: los periféricos (ADC, DAC, CTIMER, UART, pines) son los mismos del TP3.
