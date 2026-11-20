# TP4 – Filtros adaptativos LMS en la FRDM-MCXN947 (multicore)

Laboratorio 4 de **Procesamiento Digital de Señales** (FCEFyN – UNC). Consigna: `Laboratorio4.pdf`.

> **Parte 1.** Mostrar el proceso adaptativo de un filtro transversal LMS en el esquema de *identificación de planta*, con distintos μ, entrada aleatoria y entrada constante, calculando el MSE cada 100 muestras y la diferencia entre los coeficientes de la planta y los del filtro.
> **Parte 2.** Usar un FIR para correlacionar un ruido aleatorio con la referencia del procesador adaptativo, sumarle ese ruido a una senoidal que entra a la placa y demostrar que el filtro converge al óptimo que cancela el ruido, mostrándolo en el osciloscopio.
>
> **Diferencias con la consigna (indicadas por la cátedra):**
> - Todos los FIR (planta, correlador y filtro adaptativo) tienen **10 coeficientes** (`N_COEF` en `TP4_core0.c`), no 30.
> - El ruido de la Parte 2 es **AWGN**: ruido blanco gaussiano generado con el método polar de Marsaglia (`aleatorio_gaussiano()`).
> - El filtro adaptativo es **`arm_lms_q15` de CMSIS-DSP**, en punto fijo **Q15**, en las dos partes. Es la única implementación del LMS del firmware (ver [Filtro adaptativo en Q15](#filtro-adaptativo-en-q15-arm_lms_q15)).

## Arquitectura: un núcleo procesa, el otro comunica

El MCXN947 tiene dos Cortex-M33. El **core0** tiene FPU y extensiones DSP; el **core1** no tiene ninguna de las dos. Por eso:

```
                      ┌──────────────────────── MCXN947 ────────────────────────┐
 generador ─► ADC0_A0 │  core0 (TP4_core0) PROCESAMIENTO        core1 (TP4_core1)│
  (J4[2])             │  CTIMER0 MAT3 (8 kHz) ─► ADC0 ─► ISR:   COMUNICACIÓN     │
                      │    AWGN, correlador, LMS ─► DAC0/DAC1   LPUART4 (VCOM)   │ ◄──► USB MCU-Link ◄──► MATLAB
 osciloscopio ◄─ DAC0 │  Parte 1: identificación de planta        │      ▲       │
   (J1[4], J1[2]) DAC1│        │  texto (líneas CSV)              │      │       │
                      │        └──► tx[] ─── memoria compartida ──┘      │       │
                      │        ◄─── cmd[] ◄── 0x2004C000 (8 KB) ─────────┘       │
                      └──────────────────────────────────────────────────────────┘
```

- **core0** hace todo el procesamiento en tiempo real y nunca espera a la UART: deja las líneas de texto en una cola de la memoria compartida y sigue.
- **core1** vacía esa cola hacia la UART (115200 8N1), arma las líneas de comando que llegan de la PC y se las pasa al core0. Si llega una `X` al principio de una línea, avisa enseguida al core0 para que corte la corrida de la Parte 1.
- **Memoria compartida** (`memoria_compartida_t`): región `0x2004C000–0x2004DFFF`, la misma `rpmsg_sh_mem` del ejemplo multicore del SDK. La estructura está escrita en `TP4_core0.c` y copiada en `TP4_core1.c`: **las dos copias tienen que ser idénticas**. Son dos colas de un productor y un consumidor (texto core0→core1, comandos core1→core0). Cada contador lo escribe un solo núcleo, así que alcanza con barreras de memoria (`__DMB`) y no hacen falta semáforos.
- **Arranque**: el core0 inicializa la memoria compartida y arranca al core1 con el **MCMGR** (`MCMGR_StartCore`), pasándole la dirección de la memoria compartida como *startup data*. La imagen del core1 va adentro de la del core0 (región `SRAM_CORE1`, `0x2004E000`) y el IDE la copia a RAM al arrancar.

## Estructura

| Carpeta | Qué hay |
|---|---|
| `TP4_core0/` | Proyecto MCUXpresso del **core0** (master). Todo el programa está en `source/TP4_core0.c`, como en los TP anteriores: parámetros, memoria compartida, coeficientes, AWGN, LMS (`arm_lms_q15`), Parte 1, Parte 2, interrupciones, comandos y `main`. `board/` y `TP4_core0.mex`: ConfigTools. |
| `TP4_core1/` | Proyecto MCUXpresso del **core1** (slave, M33SLAVE): `source/TP4_core1.c`. `board/` y `TP4_core1.mex`: ConfigTools (pines y LPUART de la UART). |
| `matlab/` | `tp4_experimentos.m` (todos los experimentos), `tp4_captura.m`, `tp4_parse_log.m`, `tp4_graficos_p1.m`, `tp4_graficos_p2.m`, `tp4_graficos_cap.m`. |

Igual que en el TP1, TP2 y TP3, cada carpeta `TP4_coreN` es el proyecto completo del IDE: `.project`, `.cproject`, `.mex`, `board/` generado por ConfigTools y los archivos del SDK 26.06 (`CMSIS`, `drivers`, `device`, `startup`, `component`, `utilities`) más el MCMGR (`mcmgr/`).

## Configuración con ConfigTools

Pines, relojes y periféricos **no** se configuran a mano en el código: los genera ConfigTools en `board/` (pin_mux.c, clock_config.c, peripherals.c), y `main()` sólo llama a `BOARD_InitBootPins()`, `BOARD_InitBootClocks()` y `BOARD_InitBootPeripherals()`, igual que en los TP anteriores.

**TP4_core0.mex** (core0):
- **Clocks**: `BOARD_BootClockPLL150M` (150 MHz). CTIMER0 ← PLL0 (150 MHz), ADC0/DAC0/DAC1 ← FRO_HF (48 MHz), FLEXCOMM4 ← FRO 12 MHz (para la UART del core1: los relojes son globales y los configura el core0).
- **Pins**: LEDs RGB, SW2 (P0_23, GPIO0 int. 1) y SW3 (P0_6, GPIO0 int. 0) con flanco descendente, ADC0_A0, CT0_MAT3 (P0_19), INPUTMUX CTIMER0 MAT3 → trigger del ADC0, DAC0_OUT (P4_2), DAC1_OUT (P4_3), y dos GPIO de medición (`BOARD_InitTESTPins`: TRIG = P0_25, PROC = P0_26).
- **Peripherals**: CTIMER0 (MAT3 toggle, 9374 → 8 kHz, *no* arranca en la inicialización), ADC0 (canal A0, 16 bits, trigger 0 por hardware, interrupción de fin de trigger, prioridad 0), DAC0 y DAC1, GPIO0 (interrupciones de las teclas, prioridad 2).

**TP4_core1.mex** (core1):
- **Pins**: `BOARD_InitDEBUG_UARTPins` asignada al **Cortex-M33 (Core #1)**: P1_8 = FC4_P0 (RX) y P1_9 = FC4_P1 (TX).
- **Peripherals**: LP_FLEXCOMM4 como LPUART en modo *polling*, 115200 8N1, TX y RX habilitados, `rxFifoWatermark = 0`.
- **Clocks**: copia de la del core0, sólo para que el Peripherals Tool calcule la frecuencia de la UART (el core1 **no** llama a `BOARD_InitBootClocks()`).

Para cambiar algo: abrir el `.mex` del proyecto con *ConfigTools → Open Pins / Clocks / Peripherals*, editar y *Update Code*.

## Conexiones

| Señal | Pin | Conector |
|---|---|---|
| Entrada del generador (ADC0_A0) — igual que TP2/TP3 | ADC0_A0 | **J4[2]** (ARD_A0) |
| DAC0 → osciloscopio CH1 (por defecto **e**: salida del cancelador) | P4_2 | **J1[4]** |
| DAC1 → osciloscopio CH2 (por defecto **d**: señal + ruido) | P4_3 | **J1[2]** |
| CT0_MAT3: cuadrada de 8 kHz = fs (un flanco ascendente por muestra) | P0_19 | J2[13] |
| TRIG: pulso al reiniciar los coeficientes (disparo externo) | P0_25 | J2[12] |
| PROC: en alto mientras la ISR procesa una muestra | P0_26 | J2[10] |
| Masa | – | cualquier GND |

**Generador:** senoidal **200 Hz, 1 Vpp, offset 1,65 V**, salida en High-Z. Antes de conectarlo, verificarlo con el osciloscopio en acople DC: tiene que quedar entre ~1,15 V y ~2,15 V, **nunca fuera de 0–3,3 V**. Unir las masas del generador, el osciloscopio y la placa.

## Paso a paso

### 1. Importar los proyectos en MCUXpresso IDE

1. *File → Import → General → Existing Projects into Workspace → Next*.
2. *Select root directory*: la carpeta `TP4` del repo. Tienen que aparecer **TP4_core0** y **TP4_core1**; tildar los dos.
3. **No** tildar *Copy projects into workspace* (como en los otros TP: se trabaja sobre el repo).
4. *Finish*. Si el IDE pregunta por el SDK, tiene que estar instalado el **SDK_26.06.00 FRDM-MCXN947** (el mismo del TP3).

### 2. Compilar

1. Compilar **primero TP4_core1** (genera `TP4_core1.axf` y `TP4_core1.axf.o`) y **después TP4_core0**, que linkea esa imagen adentro (región `SRAM_CORE1`). El martillo sobre un proyecto (*Build Project*) **no** compila antes el otro; *Project → Build All* (Ctrl+B) sí respeta el orden.
   - Si el linker del core0 dice `No slave code for _core_m33slave` o `undefined symbol __vectors_start___core_m33slave`, el core0 no encontró `TP4_core1.axf.o`. Pasa si el core1 no está compilado o si se cambiaron archivos fuera del IDE. Seleccionar los dos proyectos → *Refresh* (F5) → *Project → Clean* de los dos → compilar TP4_core1 → compilar TP4_core0.
2. Para medir ciclos representativos conviene **-O2** (o la configuración *Release*): *Properties → C/C++ Build → Settings → MCU C Compiler → Optimization*, en los dos proyectos.

### 3. Grabar y correr

1. Conectar la placa por el USB del **MCU-Link (J17)**.
2. *Debug* sobre **TP4_core0** (sonda MCU-Link, SWD). Sólo hace falta debuggear el core0: el core1 lo arranca el core0 desde `main()`. *Resume* (F8), o cortar el debug y apretar RESET.
3. Abrir una terminal serie (**COMx del MCU-Link VCom, 115200 8N1**). Al arrancar aparece `#TP4 core1: UART lista ...` (lo manda el core1), después la ayuda y la línea `#CFG` (las manda el core0).
4. LEDs: **verde** = Parte 1, **rojo** = Parte 2, **azul** parpadea con cada bloque enviado; **azul fijo** = el core1 no arrancó (no hay comunicación).

### 4. Parte 1 (identificación de planta)

| Qué | Comandos |
|---|---|
| Una corrida, μ = 0,01, entrada aleatoria | `1` → `U0.01` → `S` (o SW2) |
| Barrido de μ (0,002 / 0,01 / 0,05 / 0,2 / 0,5 / 0,7) | `B` |
| Entrada constante | `E1` → `B` |
| Otra amplitud de entrada (potencia), 0 < A ≤ 1 | `A0.25` → `S` |
| Ruido de medición σᵥ = 0,01 | `V0.01` → `B` |
| No mandar las 10 diferencias | `D0` |
| Cortar la corrida | `X` (la atiende el core1 al instante) o SW2 |

Salida (el MSE se promedia cada 100 muestras, como pide la consigna):
```
P1I,alg=CMSIS_LMS_Q15,mu=1.0010e-02,mu_q15=328,ps=0,in=RANDOM,A=1.0000e+00,Px=3.3333e-01,L=10,...,mu_max=6.0000e-01,tau=1.4985e+02
P1P,2.7890e-01,2.3050e-01,...            ← coeficientes de la planta
P1M,100,3.8e-02,1.2e-01                   ← n, MSE del bloque, ||p-w||² (los dB los calcula MATLAB)
P1D,100,2.1e-01,...                       ← p_k - w_k de los 10 coeficientes
P1W,...                                   ← coeficientes aprendidos
P1F,5000,...,cyc,OK                       ← ciclos por muestra del filtro (arm_lms_q15 + pasaje a Q15) y estado (OK | DIVERGE | CORTADO)
```

### 5. Parte 2 (cancelación de ruido)

1. `2` (o **SW3**). Empiezan a salir líneas `P2M,n,p_ruido,p_residuo,p_senal,error_coef` 80 veces por segundo (`M0` las apaga, `M10` manda 1 de cada 10). MATLAB calcula la reducción de ruido 10·log10(p_ruido/p_residuo).
2. **Osciloscopio**: CH1 en J1[4] (e), CH2 en J1[2] (d), acople DC, 200 mV/div, 2 ms/div, centrados en 1,65 V. Se ve a la vez la senoidal con ruido y la senoidal limpia.
3. **Aprendizaje en vivo**: CH1 en J1[4], 200 ms/div, disparo externo con TRIG (J2[12]), flanco ascendente, modo *Single*. Apretar **SW2**: los coeficientes vuelven a cero y se ve cómo la salida se va limpiando. Con el μ por defecto (0,02) la constante de tiempo del modo más lento es ~0,4 s; la reducción llega a su valor final en ~1,5 s.
4. Elegir qué sale por cada DAC: `O<n>` (DAC0) y `P<n>` (DAC1) con n = 0 d, 1 e, 2 y, 3 s, 4 residuo ×4, 5 referencia.
5. Otros comandos: `U<mu>` cambia el μ de `arm_lms_q15` (0 < μ < 1, por defecto 0,02) sin borrar lo aprendido; `Z` = SW2; `Z<n>` reinicia y **captura n muestras** desde el reinicio; `K<n>` captura n muestras ya; `C` imprime los coeficientes, los óptimos de Wiener y el correlador; `T` el tiempo de cómputo por muestra; `Q0` / `Q1` cambia la topología; `A<sigma>` el desvío del AWGN (por defecto 0,25 V). La frecuencia de muestreo es la del CTIMER0 en ConfigTools (8 kHz).

**Topologías** (`p2_correlador_referencia`):
- `Q1` (por defecto, **figura 2 de la consigna**): d = s + r y la referencia es x' = C{r}. El óptimo es **W = C⁻¹ ≈ [1,400 −0,980 0 … 0]**. Como x' es ruido coloreado, la convergencia tiene varias constantes de tiempo.
- `Q0` (cancelador clásico): d = s + C{r} y la referencia es r. El óptimo es **W = C** y la convergencia tiene una sola constante de tiempo.

En los dos casos el firmware compara contra el óptimo de Wiener exacto (R·w = p de 10×10, resuelto una vez y guardado como constante `w_optimo_figura2`; para el clásico es directamente C) y manda ‖w_opt − w‖² en cada `P2M`.

## Filtro adaptativo en Q15 (`arm_lms_q15`)

El procesador adaptativo de las dos partes es `arm_lms_q15` de CMSIS-DSP, muestra a muestra (`blockSize = 1`), con las funciones `lms_iniciar()`, `lms_procesar()` y `lms_leer_coeficientes()` de `TP4_core0.c`. La planta, el correlador, el AWGN y la senoidal se calculan en `float` (son "el mundo"); la referencia x(n) y la deseada d(n) se convierten a Q15 justo antes de entrar al filtro.

Esto es lo que hace `arm_lms_q15` en cada muestra y lo que implica:

| Paso en CMSIS | Consecuencia |
|---|---|
| y = SSAT16(Σ b·x >> (15 − postShift)) | Los coeficientes valen b·2^postShift/32768, con rango ±2^postShift. |
| e = (q15_t)(d − y) | **No satura**: si \|d − y\| ≥ 1 el valor da la vuelta. Las señales tienen que quedar holgadas dentro de ±1. |
| α = (e·μ) >> 15; b += (α·x) >> 15 | El paso efectivo es **μ_ef = 2^postShift · μ**. Los dos `>> 15` truncan hacia −∞: sesgo de −½ LSB por actualización y, si \|e·μ·x\| < 1 LSB, el filtro **deja de aprender** (*stalling*). |

Escalado elegido:

- **Parte 1**: postShift = 0, así que μ_ef = μ y la teoría se aplica directo con μ. La entrada cumple |x| ≤ A ≤ 1 y la planta está escalada para que Σ|p_k| = 0,896 < 1, de modo que d(n) nunca satura.
- **Parte 2**: fondo de escala **1,0 en Q15 = 1,65 V**, es decir todo el rango del ADC/DAC (0–3,3 V) alrededor de la continua. postShift = 1 (μ_ef = 2μ) porque el óptimo C⁻¹ tiene coeficientes de hasta 1,4. Con la senoidal de 0,5 V y σ = 0,25 V, d(n) queda dentro de ±1,5 V (4σ) y e = d − y no se sale del rango.

Lo que hay que esperar en la placa (verificado con un modelo bit a bit de `arm_lms_q15` en Python):

- **Parte 1, entrada aleatoria con A = 1**: converge hasta μ = 0,5 y diverge con μ = 0,7, como predice μ_max = 0,6. En Q15 la divergencia se ve como coeficientes saturados y el estado queda en `DIVERGE`.
- **Parte 1, piso de cuantización**: el MSE no baja indefinidamente. El piso queda en ~−28, −45, −57 y −67 dB para μ = 0,002, 0,01, 0,05 y 0,2. **Baja al aumentar μ**, al revés que el desajuste por ruido.
- **Parte 1, poca potencia (A = 0,25)**: con μ = 0,002 directamente no aprende (*stalling*). Con μ ≥ 0,05 sí aprende.
- **Parte 2**: hay un μ óptimo. Con μ chico manda el piso de Q15 y con μ grande el desajuste que provoca la propia senoidal en el error. Con σ = 0,25 V la reducción de ruido queda en:

  | μ (`arm_lms_q15`) | Figura 2 (Q1): Q15 / float | Clásico (Q0): Q15 / float |
  |---|---|---|
  | 0,01 | 15,0 / 16,8 dB | 14,1 / 28,0 dB |
  | 0,02 (por defecto) | 13,7 / 13,9 dB | 18,4 / 23,5 dB |
  | 0,05 | 9,7 / 9,8 dB | 17,1 / 17,2 dB |

  En el cancelador clásico, con μ chico, el truncamiento de CMSIS se come casi 14 dB.

### 6. MATLAB

1. **Cerrar la terminal** (MATLAB no puede abrir el COM si otro programa lo tiene abierto).
2. En `TP4/matlab`, abrir `tp4_experimentos.m`, ajustar `PUERTO` y correr por secciones (Ctrl+Enter):
   - **E1–E5**: Parte 1 (barrido de μ con entrada aleatoria y constante, potencia de entrada, piso de cuantización de Q15 con A = 0,25, ruido de medición).
   - **E6**: Parte 2 con la topología de la figura 2 y distintos μ (reducción de ruido y convergencia a W = C⁻¹).
   - **E7**: Parte 2 con el cancelador clásico (W = C).
   - **E8**: captura de formas de onda desde el reinicio (`Z2000`) y ya convergido (`K2000`): d, e, s, ruido estimado y espectros.
3. Los registros quedan en `registros/` y las figuras en `img/`.

Mientras el core1 manda una captura (~10 s para 2000 muestras a 115200 baudios), el core0 sigue cancelando el ruido en tiempo real: en el osciloscopio no se nota nada. Esa es la ventaja de separar procesamiento y comunicación.

## Comandos (resumen)

| Comando | Parte | Acción |
|---|---|---|
| `1` / `2` | – | Parte 1 / Parte 2 (también SW3) |
| `?` | – | ayuda y configuración (`#CFG`) |
| `U<mu>` | 1, 2 | μ de `arm_lms_q15` (fracción Q15, 0 < μ < 1) |
| `A<x>` | 1, 2 | P1: amplitud de la entrada (0 < A ≤ 1) · P2: desvío del AWGN (V) |
| `E<0\|1>`, `V<σ>`, `N<n>`, `R<semilla>`, `D<0\|1>` | 1 | entrada aleatoria/constante, ruido de medición, muestras, semilla, diferencias |
| `S`, `B`, `X` | 1 | arrancar, barrido de μ, cortar |
| `Q<0\|1>` | 2 | topología: correlador en el ruido / en la referencia (figura 2) |
| `O<n>`, `P<n>` | 2 | señal del DAC0 / DAC1 |
| `Z[n]`, `K<n>` | 2 | reiniciar coeficientes (y capturar n muestras), capturar n muestras |
| `C`, `T`, `M<k>` | 2 | coeficientes, tiempos, 1 de cada k bloques |

## Valores teóricos (L = 10)

CMSIS usa la convención w ← w + μ_ef·e·x, que equivale a 2μ de la teoría de Widrow, con **μ_ef = 2^postShift·μ**. La condición de convergencia queda **0 < μ_ef < 2/tr(R) = 2/(L·Px)**, con Px en unidades de Q15. La tabla da μ_max y τ en las unidades del μ de `arm_lms_q15`, que es lo que se escribe con `U`.

| Caso | Px (Q15) | μ_max | τ del MSE |
|---|---|---|---|
| P1 aleatoria, A = 1 (ps = 0) | 1/3 | 0,6 | 1,5/μ muestras (μ = 0,01 → 150) |
| P1 constante, A = 1 (ps = 0) | 1 (λ = L·A² = 10) | 0,2 | 1/(20μ) muestras |
| P2 figura 2 (Q1), σ = 0,25 V (ps = 1) | 0,0229 | 4,4 (> 1: no puede volverse inestable) | modos entre 2,6/μ y 60/μ muestras (dispersión de autovalores ≈ 23; μ = 0,02 → τ lento ≈ 0,38 s a 8 kHz) |
| P2 clásico (Q0), σ = 0,25 V (ps = 1) | 0,0229 | 4,4 | 10,9/μ muestras (μ = 0,02 → 68 ms) |

Con la topología de la figura 2, el óptimo de 10 coeficientes deja un ruido residual de 6·10⁻⁴·σ² (≈ 32 dB de reducción como máximo). El límite viene de truncar C⁻¹ a 10 coeficientes. En la práctica, el piso de Q15 y el desajuste por la senoidal limitan antes (ver la tabla de la sección Q15).

## Plan B: si el IDE no acepta los proyectos

Los `.cproject` se armaron a partir del del TP3 (mismo IDE 25.6 y SDK 26.06) con las opciones multicore del ejemplo `multicore_examples/hello_world` del SDK. Si el IDE no los importa bien, se pueden rehacer así:

1. *Quickstart → Import SDK example(s)* → frdmmcxn947 → `multicore_examples/hello_world` (crea los dos proyectos enlazados, primary y secondary).
2. Renombrarlos `TP4_core0` y `TP4_core1`.
3. Al core0, con *Manage SDK Components*, agregarle: ctimer, lpadc, dac_1, inputmux, gpio, mcx_spc y CMSIS-DSP.
4. Reemplazar `source/`, `board/` y el `.mex` de cada uno por los de este repo.
5. En el core0, borrar el símbolo `CR_INTEGER_PRINTF` (*Properties → C/C++ Build → Settings → MCU C Compiler → Preprocessor*): el programa usa `snprintf` con flotantes (`%.4e`).

## Verificación

- **No se compiló** (por decisión del usuario, solo hubo revisión estática). Lo primero es compilar en el IDE.
- Los drivers del core1 se sacaron del zip del SDK 26.06 instalado y son idénticos byte a byte a los que el IDE copió en el TP3. Los `.mex`, `.project` y `.cproject` son XML válidos.
- Los coeficientes y el óptimo de Wiener se verificaron numéricamente en Python: C es de fase mínima (ceros en |z| = 0,7) y el óptimo exacto para Q1 es [1,3995 −0,9795 ≈0 … 0,014].
- Los escalados de Q15 (planta ×0,4, postShift, fondo de escala de 1,65 V, σ y μ por defecto) se eligieron con un modelo bit a bit de `arm_lms_q15` en Python; de ahí salen los números de la sección Q15. Falta confirmarlos en la placa.
- El core0 escribe las líneas con `snprintf("%.4e")`. Por eso el proyecto no define `CR_INTEGER_PRINTF`: con ese símbolo, Redlib usa un `printf` solo de enteros (guía del IDE 25.06, sección 19.5.1.2).
- `documentacion/TP4` (el informe) corresponde a la versión anterior (L = 8, un solo núcleo) y no se modificó.
