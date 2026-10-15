# Coeficientes de los filtros FIR del TP2

Coeficientes de los cuatro filtros de la consigna para las cinco fs, diseñados **sin MATLAB** con [`generar_coeficientes.py`](generar_coeficientes.py). Son los diseños recomendados en los análisis:

- [pasa bajos](../analisis-filtro-pasa-bajos.md)
- [pasa altos](../analisis-filtro-pasa-altos.md)
- [pasa banda](../analisis-filtro-pasa-banda.md)
- [elimina banda](../analisis-filtro-elimina-banda.md)

## Qué hay en esta carpeta

| Archivo | Qué es |
|---|---|
| [`filtros.h`](filtros.h) | **Header listo para el firmware.** Mismo formato que `TP2/source/filtros.h`: mismos nombres, tablas `coef_tabla` / `taps_tabla`, Q15 en orden invertido y cero de relleno |
| `pasa_bajos/`, `pasa_altos/`, `pasa_banda/`, `elimina_banda/` | Un CSV por fs (`n, h, h_q15`) con los coeficientes en punto flotante y en Q15 (orden natural h[0] … h[N]), y `respuesta.png` con la respuesta de las 5 fs |
| [`verificacion.json`](verificacion.json) | Orden, método y verificación de cada diseño |
| [`generar_coeficientes.py`](generar_coeficientes.py) | El generador. Reemplaza a `filterDesigner` y a `generar_header_desde_fdatool.m` |

## Especificaciones

| Filtro | Bandas (Hz) | Apass | Astop de diseño (consigna) |
|---|---|---|---|
| PB | paso 0–3600, rechazo 3900–fs/2 | 1 dB | 31 dB (30) |
| PA | rechazo 0–5, paso 35–fs/2 | 1 dB | 31 dB (30) |
| PBanda | rechazo 0–5, paso 35–3500, rechazo 3560–fs/2 | 1 dB | 31 dB (30) |
| EB fiel | paso 0–35, rechazo 42,5–57,5, paso 65–fs/2 | 1 dB | 26 dB (25) |
| EB liviana | paso 0–20, rechazo 42,5–57,5, paso 80–fs/2 | 1 dB | 26 dB (25) |

**Qué elimina banda va en `filtros.h`:** la versión **fiel a 8 kHz** y la **liviana de 16 a 48 kHz**.

- La fiel es la más fiel a la consigna, pero solo entra en tiempo real a 8 kHz.
- La liviana entra a 16 kHz. Arriba de eso ningún FIR entra, y la liviana es la que menos sobrecarga.
- Los CSV de la fiel a 16 y 22 kHz también están en `elimina_banda/`, por si los querés usar.
- La fiel a 44 y 48 kHz no se generó: tendría ~6100 y ~6650 coeficientes, no entra en tiempo real y su diseño por programación lineal es demasiado pesado.

El detalle de esta decisión está en la sección 12 del [análisis del elimina banda](../analisis-filtro-elimina-banda.md).

## Verificación

| Diseño | fs | Orden N | Taps en el header | Método | Rechazo (flotante) | Rechazo (Q15) | Paso en Q15 (mín / máx) | Consigna en Q15 |
|---|---|---|---|---|---|---|---|---|
| PB | 8 kHz | 33 | 34 | Remez | −33,44 dB | −33,42 dB | −0,39 / +0,37 dB | ✓ |
| PB | 16 kHz | 63 | 64 | Remez | −31,17 dB | −31,19 dB | −0,50 / +0,47 dB | ✓ |
| PB | 22 kHz | 87 | 88 | Remez | −31,21 dB | −31,22 dB | −0,50 / +0,47 dB | ✓ |
| PB | 44 kHz | 173 | 174 | Remez | −31,03 dB | −30,96 dB | −0,51 / +0,48 dB | ✓ |
| PB | 48 kHz | 189 | 190 | Remez | −31,09 dB | −31,04 dB | −0,51 / +0,48 dB | ✓ |
| PA | 8 kHz | 260 | 262 | Remez | −31,15 dB | −31,22 dB | −0,51 / +0,48 dB | ✓ |
| PA | 16 kHz | 518 | 520 | Remez | −31,03 dB | −31,00 dB | −0,52 / +0,49 dB | ✓ |
| PA | 22 kHz | 712 | 714 | Remez | −31,02 dB | −30,89 dB | −0,52 / +0,49 dB | ✓ |
| PA | 44 kHz | 1432 | 1434 | Prog. lineal | −31,20 dB | −31,36 dB | −0,53 / +0,50 dB | ✓ |
| PA | 48 kHz | 1562 | 1564 | Prog. lineal | −31,20 dB | −31,18 dB | −0,52 / +0,50 dB | ✓ |
| PBanda | 8 kHz | 260 | 262 | Remez | −31,10 dB | −31,05 dB | −0,51 / +0,48 dB | ✓ |
| PBanda | 16 kHz | 520 | 522 | Remez | −31,08 dB | −30,95 dB | −0,51 / +0,48 dB | ✓ |
| PBanda | 22 kHz | 714 | 716 | Remez | −31,04 dB | −30,89 dB | −0,52 / +0,49 dB | ✓ |
| PBanda | 44 kHz | 1434 | 1436 | Prog. lineal | −31,04 dB | −30,78 dB | −0,52 / +0,49 dB | ✓ |
| PBanda | 48 kHz | 1564 | 1566 | Prog. lineal | −31,03 dB | −30,79 dB | −0,52 / +0,49 dB | ✓ |
| EB fiel | 8 kHz | 1108 | 1110 | Remez | −26,10 dB | −26,06 dB | −0,51 / +0,48 dB | ✓ |
| EB fiel | 16 kHz | 2218 | — (2220) | Remez | −26,17 dB | −26,15 dB | −0,52 / +0,49 dB | ✓ |
| EB fiel | 22 kHz | 3050 | — (3052) | Prog. lineal | −26,20 dB | −26,16 dB | −0,52 / +0,49 dB | ✓ |
| EB liviana | 8 kHz | 450 | — (452) | Remez | −26,07 dB | −26,02 dB | −0,51 / +0,48 dB | ✓ |
| EB liviana | 16 kHz | 898 | 900 | Remez | −26,02 dB | −26,00 dB | −0,52 / +0,49 dB | ✓ |
| EB liviana | 22 kHz | 1236 | 1238 | Remez | −26,04 dB | −25,99 dB | −0,52 / +0,49 dB | ✓ |
| EB liviana | 44 kHz | 2488 | 2490 | Prog. lineal | −26,17 dB | −25,83 dB | −0,53 / +0,49 dB | ✓ |
| EB del header anterior ¹ | 48 kHz | 7602 | 7604 | Kaiser | −26,02 dB | −26,02 dB | −0,16 / +0,19 dB | ✓ |

Cómo leer la tabla:

- **Rechazo**: la peor ganancia dentro de la banda de rechazo. La consigna pide ≤ −30 dB (PB, PA, PBanda) y ≤ −25 dB (EB). Se diseñó con 1 dB de margen (31 y 26 dB), así que el peor caso cuantizado a Q15 sigue cumpliendo con margen.
- **Paso en Q15**: el ripple de la banda de paso con los coeficientes cuantizados. El diseño apunta a ±0,51 dB (Apass 1 dB pico a pico). En Q15 algunos filtros largos llegan a −0,53 dB, unas milésimas de dB que no se ven en el osciloscopio. La consigna no fija el Apass.
- **Taps en el header**: N + 1 coeficientes, más un cero de relleno si la cantidad es impar (CMSIS pide una cantidad par). "—" indica que ese diseño no va en `filtros.h` (el número entre paréntesis es lo que ocuparía).
- **Consigna en Q15**: el rechazo cuantizado cumple el Astop de la consigna y no hay bultos en las transiciones.

¹ **EB a 48 kHz (provisorio):** el diseño Equiripple liviano a 48 kHz tarda horas en calcularse. Mientras tanto, `filtros.h` usa en esa posición el elimina banda Kaiser del header anterior (35 / 42,5 / 57,5 / 65, Astop 25, 7603 coeficientes). Cumple la consigna, pero por eso `MAX_TAPS` sigue en 7604. Cuando termine el diseño Equiripple, `MAX_TAPS` baja a ~2716. A 48 kHz ningún elimina banda FIR entra en tiempo real, así que en la práctica no cambia nada.

**Simulación en C.** `filtros.h` se compiló con gcc y se filtraron senoidales con la misma aritmética que `arm_fir_q15`: acumulador de 64 bits, `>> 15`, saturación y coeficientes invertidos. Los 20 filtros dan cantidad de taps par, `MAX_TAPS` correcto y estas ganancias:

| Filtro | Frecuencia de prueba | Peor caso en las 5 fs |
|---|---|---|
| PB | 3950 Hz (rechazo) | −34,2 dB (−43,9 a 8 kHz) |
| PA | 5 Hz (borde del rechazo) | −30,9 dB |
| PA | 3 Hz | −45,1 dB |
| PBanda | 3 Hz / 3600 Hz | −45,7 / −31,2 dB |
| EB | 45, 50 y 55 Hz | −26,0 dB |
| Todos | banda de paso (35 Hz, 1 kHz, 3500 Hz) | entre −0,5 y +0,5 dB (en el EB liviano, 35 y 65 Hz quedan en la transición: −10 dB) |

La verificación final es en la placa: medir con el osciloscopio contra el modo BYPASS.

## Cómo usarlo en el firmware

1. Reemplazar el header actual, que queda guardado en git:

   ```bash
   cp documentacion/TP2/coeficientes/filtros.h TP2/source/filtros.h
   ```

2. Compilar y grabar desde MCUXpresso como siempre. `TP2.c` no necesita cambios: usa `NUM_FS`, `NUM_TIPOS`, `MAX_TAPS`, `coef_tabla` y `taps_tabla`, que mantienen sus nombres.
3. **Tiempo real.** Según las estimaciones de los análisis, con `arm_fir_q15` entran:

   | Filtro | 8 kHz | 16 kHz | 22 kHz | 44 kHz | 48 kHz |
   |---|---|---|---|---|---|
   | PB | ✓ | ✓ | ✓ | ✓ | ✓ |
   | PA | ✓ | ✓ | ⚠ | ✗ | ✗ |
   | PBanda | ✓ | ✓ | ⚠ | ✗ | ✗ |
   | EB | ✓ | ⚠ | ✗ | ✗ | ✗ |

   Donde dice ✗, la ISR tarda más que el período de muestreo. Hay que medirlo con el contador DWT (sección 11 del análisis del pasa altos). `SOLO_8K` en `TP2.c` sigue disponible para probar todo con la fila de 8 kHz.

## Cómo regenerarlos

```bash
pip install numpy scipy matplotlib
cd documentacion/TP2/coeficientes
python generar_coeficientes.py            # diseña todo: tarda varias horas (los EB de 44/48 kHz)
python generar_coeficientes.py --reusar   # reusa los CSV y solo rehace filtros.h, gráficos y verificación
```

Para cambiar una especificación, editar el diccionario `DISENOS` al principio del script.

**Métodos:**

- Equiripple (Parks-McClellan) con `scipy.signal.remez` cuando converge.
- Cuando no converge (órdenes altos con bandas de pocos Hz), el mismo óptimo minimax se calcula por **programación lineal** (`scipy.optimize.linprog`). Da el mismo filtro que daría `filterDesigner` con Equiripple, salvo diferencias de unos pocos coeficientes en el orden.
