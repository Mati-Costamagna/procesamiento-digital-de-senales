# Procesamiento Digital de Señales

Trabajos de laboratorio de **Procesamiento Digital de Señales** (FCEFyN – UNC) sobre la placa **FRDM-MCXN947**, con MCUXpresso IDE, CMSIS-DSP y MATLAB.

## Trabajos prácticos

| TP | Tema | Código | Informe |
|---|---|---|---|
| 1 | Muestreo con MCXN947 | [`TP1/`](TP1) | [`documentacion/TP1/Informe_TP1_DSP.pdf`](documentacion/TP1/Informe_TP1_DSP.pdf) |
| 2 | Filtros FIR con CMSIS-DSP en MCXN947 | [`TP2/`](TP2) | [`documentacion/TP2/main.pdf`](documentacion/TP2/main.pdf) |
| 3 | FFT con CMSIS-DSP en MCXN947 | [`TP3/`](TP3) | [`documentacion/TP3/main.pdf`](documentacion/TP3/main.pdf) |
| 4 | Filtros adaptativos LMS en MCXN947 (multicore) | [`TP4/`](TP4) | [`documentacion/TP4/main.pdf`](documentacion/TP4/main.pdf) |

## Estructura

```
TPn/                  Proyecto MCUXpresso completo del TP n (.project, .cproject, .mex,
                      board/ de ConfigTools, source/ y archivos del SDK).
                      TP4 tiene dos proyectos, TP4_core0 y TP4_core1, y su README.
TPn/matlab/           Scripts de MATLAB del TP (TP3 y TP4).
documentacion/TPn/    Informe en LaTeX: main.tex, PDF compilado, informe-fcefyn.sty,
                      img/ con figuras y src/ con scripts auxiliares.
```

Los informes incluyen el código del anexo con rutas relativas (por ejemplo `../../TP3/source/TP3.c`), así que hay que mantener las carpetas `TPn/` y `documentacion/TPn/` en la raíz del repositorio.

## Ramas

La versión final de cada TP está en `main`. Las ramas de trabajo (`informe-tpN`, `tp3-fft`, `touchpad-led-tp2`, etc.) se conservan como historial.
