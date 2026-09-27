# Imágenes del informe del TP1

Guardar cada captura con **exactamente** este nombre (en PNG). Mientras un
archivo falte, el PDF muestra en su lugar un recuadro rojo con el nombre esperado.

| Archivo | Qué capturar | Sección |
|---|---|---|
| `nuevo-proyecto.png` | Asistente de importación: placa, SDK, ejemplo `lpadc_interrupt` y drivers | 2.2 |
| `clocks.png` | Config Tools › Clocks: PLL0 a 150 MHz → núcleo y CTIMER0; FRO_HF → ADC0 y DAC0 | 2.3 |
| `pins.png` | Config Tools › Pins: ADC0_A0, DAC0_OUT (P4_2), CT0_MAT3 (P0_19), LED y teclas | 2.4 |
| `peripherals-ctimer.png` | Config Tools › Peripherals › CTIMER0 (match 3, reset, toggle) | 2.5.1 |
| `peripherals-lpadc.png` | Config Tools › Peripherals › ADC0 (comando 1, trigger 0, interrupción) | 2.5.2 |
| `peripherals-dac.png` | Config Tools › Peripherals › DAC0 | 2.5.3 |
| `peripherals-gpio.png` | Config Tools › Peripherals › GPIO0 (interrupciones línea 0 y 1) | 2.5.4 |
| `osc-disparo.png` | Osciloscopio en J2[13] (P0_19): señal de disparo, con la frecuencia medida | 5.2 |
| `osc-8k.png` | Entrada (CH1) y salida del DAC en J1[4] (CH2) a 8 kS/s | 5.3 |
| `osc-48k.png` | Entrada (CH1) y salida del DAC (CH2) a 48 kS/s | 5.3 |
| `osc-stop.png` | Salida del DAC en modo STOP (repite el buffer) | 5.3 |
| `debug-buffer.png` | Depurador con el programa detenido: arreglo `samples` en Expressions o Memory | 5.3 |

Si se prefiere otro formato (por ejemplo `.jpg`), cambiar la extensión en la
línea `\captura{img/...}` correspondiente de `main.tex`.

Logos: `unc_logo.png` (convertido de `unc_logo.svg`) y `fcefyn_logo.png`
(recorte de `fcefyn_logo.jpg`).
