"""Genera los coeficientes FIR del TP2 (reemplaza a filterDesigner + generar_header_desde_fdatool.m).

Diseña los filtros recomendados en documentacion/TP2/analisis-filtro-*.md, los verifica contra su
plantilla (antes y después de pasarlos a Q15) y escribe:

    coeficientes/filtros.h                   header listo para copiar a TP2/source/
    coeficientes/<filtro>/<NOMBRE>_<fs>.csv  n, h[n] (punto flotante), h[n] en Q15
    coeficientes/<filtro>/respuesta.png      respuesta en frecuencia de las 5 fs, con la plantilla
    coeficientes/verificacion.json           orden, método y verificación de cada diseño

Uso (requiere numpy, scipy y matplotlib):
    python generar_coeficientes.py

Métodos:
  - Equiripple (Parks-McClellan) con scipy.signal.remez cuando converge.
  - Cuando no converge (órdenes de más de ~1000 con bandas a pocos Hz), el mismo óptimo minimax
    se calcula por programación lineal (scipy.optimize.linprog, HiGHS). Es más lento: el script
    completo tarda del orden de una hora.
"""
import json
import math
import os
import sys
import time

import numpy as np
from scipy import optimize, signal

AQUI = os.path.dirname(os.path.abspath(__file__))
FSS = [8000, 16000, 22000, 44000, 48000]
SUF = {8000: "8k", 16000: "16k", 22000: "22k", 44000: "44k", 48000: "48k"}


# ============================================================================ especificaciones
def deltas(ap, ast):
    g = 10 ** (ap / 20)
    return (g - 1) / (g + 1), 10 ** (-ast / 20)


class Spec:
    """bandas: lista de (f0, f1, deseado) en Hz, con deseado 1 (paso) o 0 (rechazo)."""

    def __init__(self, fs, bandas, ap, ast, par):
        self.fs, self.bandas, self.ap, self.ast, self.par = fs, sorted(bandas), ap, ast, par
        self.dp, self.ds = deltas(ap, ast)

    def delta(self, d):
        return self.dp if d == 1 else self.ds

    def min_df(self):
        b = self.bandas
        return min(b[i + 1][0] - b[i][1] for i in range(len(b) - 1))


# Diseños recomendados (ver los analisis-filtro-*.md). N0 = orden estimado (proporcional a fs).
DISENOS = {
    "PB": dict(carpeta="pasa_bajos", titulo="Pasa bajos: Fpass 3600, Fstop 3900, Apass 1, Astop 31",
               bandas=lambda fs: [(0, 3600, 1), (3900, fs / 2, 0)], ap=1.0, ast=31.0, par=False,
               N0=lambda fs: 33 * fs / 8000),
    "PA": dict(carpeta="pasa_altos", titulo="Pasa altos: Fstop 5, Fpass 35, Apass 1, Astop 31",
               bandas=lambda fs: [(0, 5, 0), (35, fs / 2, 1)], ap=1.0, ast=31.0, par=True,
               N0=lambda fs: 260 * fs / 8000),
    "PBanda": dict(carpeta="pasa_banda", titulo="Pasa banda: 5 / 35 / 3500 / 3560, Apass 1, Astop 31",
                   bandas=lambda fs: [(0, 5, 0), (35, 3500, 1), (3560, fs / 2, 0)], ap=1.0, ast=31.0, par=True,
                   N0=lambda fs: 260 * fs / 8000),
    "EB_fiel": dict(carpeta="elimina_banda", titulo="Elimina banda (fiel): 35 / 42,5 / 57,5 / 65, Apass 1, Astop 26",
                    bandas=lambda fs: [(0, 35, 1), (42.5, 57.5, 0), (65, fs / 2, 1)], ap=1.0, ast=26.0, par=True,
                    N0=lambda fs: 1108 * fs / 8000, fss=[8000, 16000, 22000]),
    "EB_liviana": dict(carpeta="elimina_banda", titulo="Elimina banda (liviana): 20 / 42,5 / 57,5 / 80, Apass 1, Astop 26",
                       bandas=lambda fs: [(0, 20, 1), (42.5, 57.5, 0), (80, fs / 2, 1)], ap=1.0, ast=26.0, par=True,
                       N0=lambda fs: 450 * fs / 8000),
}
# Qué variante del elimina banda va en filtros.h para cada fs (ver analisis-filtro-elimina-banda.md, §12):
# la fiel donde entra en tiempo real (8 kHz) y la liviana en el resto (a 16 kHz entra; arriba ningún FIR entra
# y la liviana es la que menos sobrecarga).
EB_EN_HEADER = {8000: "EB_fiel", 16000: "EB_liviana", 22000: "EB_liviana", 44000: "EB_liviana", 48000: "EB_liviana"}


# ============================================================================ verificación
def respuesta(h, f, fs):
    _, H = signal.freqz(h, worN=np.asarray(f, float), fs=fs)
    return np.abs(H)


def verificar(h, spec):
    """Mide la respuesta real: peor rechazo, mínimo y máximo de la banda de paso y máximo en las transiciones."""
    stop, pmin, pmax, trans = 0.0, 1e9, 0.0, 0.0
    paso = spec.min_df() / 40
    for f0, f1, d in spec.bandas:
        f = np.unique(np.r_[np.arange(f0, f1, paso), f1])
        if len(f) > 400000:  # bandas de paso enormes: igual densidad cerca de los bordes, algo menos en el medio
            f = np.unique(np.r_[np.linspace(f0, f1, 400000), np.arange(f0, min(f1, f0 + 200 * spec.min_df()), paso),
                                np.arange(max(f0, f1 - 200 * spec.min_df()), f1, paso)])
        a = respuesta(h, f, spec.fs)
        if d == 1:
            pmin, pmax = min(pmin, a.min()), max(pmax, a.max())
        else:
            stop = max(stop, a.max())
    for i in range(len(spec.bandas) - 1):
        f = np.linspace(spec.bandas[i][1], spec.bandas[i + 1][0], 4000)
        trans = max(trans, respuesta(h, f, spec.fs).max())
    db = lambda x: 20 * math.log10(max(x, 1e-12))
    ok = stop <= spec.ds * (1 + 1e-9) and pmin >= 1 - spec.dp - 1e-9 and pmax <= 1 + spec.dp + 1e-9 \
        and trans <= 1 + spec.dp + 1e-9
    return dict(ok=bool(ok), rechazo_db=round(db(stop), 2), paso_min_db=round(db(pmin), 3),
                paso_max_db=round(db(pmax), 3), transicion_max_db=round(db(trans), 2))


def q15(h):
    return np.clip(np.round(np.asarray(h) * 32768), -32768, 32767).astype(int)


# ============================================================================ diseño
def remez(N, spec):
    bordes, des, pesos = [], [], []
    for f0, f1, d in spec.bandas:
        bordes += [f0, f1]
        des.append(d)
        pesos.append(1 / spec.delta(d))
    for dens in (16, 32, 8):
        try:
            h = signal.remez(N + 1, bordes, des, weight=pesos, fs=spec.fs, grid_density=dens, maxiter=200)
            if verificar(h, spec)["ok"]:
                return h
        except Exception:
            pass
    return None


def minimax_lp(N, spec, ajuste=0.97, k=8):
    """Óptimo minimax (el mismo de Parks-McClellan) por programación lineal. Tipo I (N par).

    Se diseña con tolerancias un 3 % más estrictas para absorber lo que pasa entre los puntos de la grilla.
    """
    assert N % 2 == 0
    fs, M = spec.fs, N // 2
    filas, lado = [], []
    for f0, f1, d in spec.bandas:
        n = int(max(200, k * (M + 1) * (f1 - f0) / (fs / 2)))
        borde = 5 * spec.min_df()
        f = np.unique(np.r_[np.linspace(f0, f1, n), np.linspace(f0, min(f1, f0 + borde), 300),
                            np.linspace(max(f0, f1 - borde), f1, 300)])
        dl = spec.delta(d) * ajuste
        C = np.cos(np.outer(2 * np.pi * f / fs, np.arange(M + 1))) / dl
        D = np.full(len(f), d / dl)
        filas += [np.c_[C, -np.ones(len(f))], np.c_[-C, -np.ones(len(f))]]
        lado += [D, -D]
    A = np.vstack(filas)
    b = np.concatenate(lado)
    c = np.zeros(A.shape[1])
    c[-1] = 1
    r = optimize.linprog(c, A_ub=A, b_ub=b, bounds=[(None, None)] * A.shape[1], method="highs-ipm")
    if not r.success:
        raise RuntimeError(r.message)
    a = r.x[:-1]
    h = np.zeros(N + 1)
    h[M] = a[0]
    h[M - np.arange(1, M + 1)] = a[1:] / 2
    h[M + np.arange(1, M + 1)] = a[1:] / 2
    return h


def disenar(spec, N0):
    """Menor N que cumple, buscando con remez alrededor de N0; si remez no converge, programación lineal."""
    st = 2 if spec.par else 1
    norm = lambda n: int(n) + (int(n) % 2 if spec.par else 0)
    N = norm(math.ceil(N0))
    h = remez(N, spec)
    if h is not None:
        # bajar mientras siga cumpliendo
        while True:
            h2 = remez(N - st, spec)
            if h2 is None:
                return N, h, "remez"
            N, h = N - st, h2
    # subir un poco por si N0 quedó corto
    for k in range(1, 6):
        h = remez(N + k * st, spec)
        if h is not None:
            return N + k * st, h, "remez"
    # remez no converge: programación lineal (siempre tipo I)
    N = int(math.ceil(N0)) + (int(math.ceil(N0)) % 2)
    for _ in range(10):
        h = minimax_lp(N, spec)
        if verificar(h, spec)["ok"]:
            return N, h, "programación lineal"
        N += 2
    raise RuntimeError("no se pudo diseñar")


# ============================================================================ salida
def escribir_csv(ruta, h):
    hq = q15(h)
    with open(ruta, "w", encoding="utf-8") as f:
        f.write("n,h,h_q15\n")
        for n, (v, q) in enumerate(zip(h, hq)):
            f.write(f"{n},{v:.12e},{q}\n")


def taps_cmsis(h):
    return len(h) + (len(h) % 2)


def arreglo_c(nombre, h):
    """Coeficientes Q15 en orden invertido (CMSIS) y con un cero al final si la cantidad es impar."""
    hq = list(q15(h)[::-1])
    if len(hq) % 2:
        hq.append(0)
    macro = f"NUM_TAPS_{nombre.split('_')[0].upper()}_{nombre.split('_')[1].upper()}"
    lineas = [f"#define {macro} {len(hq)}", f"static const q15_t {nombre}[{macro}] = {{"]
    for i in range(0, len(hq), 12):
        lineas.append("  " + ", ".join(str(v) for v in hq[i:i + 12]) + ",")
    lineas.append("};")
    return "\n".join(lineas), len(hq)


def escribir_header(ruta, elegidos):
    tipos = [("PB", "PB"), ("PA", "PA"), ("PBanda", "PBanda"), ("EB", None)]
    bloques, taps = [], {}
    for fs in FSS:
        for nombre_c, clave in tipos:
            clave = clave or EB_EN_HEADER[fs]
            h = elegidos[(clave, fs)]["h"]
            texto, n = arreglo_c(f"{nombre_c}_{SUF[fs]}", h)
            bloques.append(texto)
            taps[(nombre_c, fs)] = n
    max_taps = max(taps.values())
    cab = f"""/*
 * filtros.h - generado por documentacion/TP2/coeficientes/generar_coeficientes.py
 * Filtros FIR Equiripple (Parks-McClellan), coeficientes Q15 en orden invertido (CMSIS),
 * con un cero de relleno al final cuando la cantidad de coeficientes es impar.
 * Incluir SOLO desde un .c (los arreglos son static const).
 *
 * PB     : {DISENOS['PB']['titulo'].split(': ')[1]}
 * PA     : {DISENOS['PA']['titulo'].split(': ')[1]}
 * PBanda : {DISENOS['PBanda']['titulo'].split(': ')[1]}
 * EB     : 8 kHz {DISENOS['EB_fiel']['titulo'].split(': ')[1]}
 *          16-48 kHz {DISENOS['EB_liviana']['titulo'].split(': ')[1]}
 */

#ifndef FILTROS_H_
#define FILTROS_H_

#include "arm_math.h"

#define NUM_FS     5
#define NUM_TIPOS  4
#define MAX_TAPS   {max_taps}

enum {{ PB = 0, PA, PBANDA, EB }};
enum {{ K8 = 0, K16, K22, K44, K48 }};

static const uint32_t fs_hz[NUM_FS] = {{8000, 16000, 22000, 44000, 48000}};
static const char * const nombre_tipo[NUM_TIPOS] = {{"PB", "PA", "PBanda", "EB"}};

"""
    tabla_c = ["static const q15_t * const coef_tabla[NUM_FS][NUM_TIPOS] = {"]
    tabla_t = ["static const uint16_t taps_tabla[NUM_FS][NUM_TIPOS] = {"]
    for fs in FSS:
        s = SUF[fs]
        tabla_c.append(f"  {{PB_{s}, PA_{s}, PBanda_{s}, EB_{s}}},")
        tabla_t.append("  {" + ", ".join(str(taps[(t, fs)]) for t, _ in tipos) + "},")
    tabla_c.append("};")
    tabla_t.append("};")
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(cab + "\n\n".join(bloques) + "\n\n" + "\n".join(tabla_c) + "\n\n" + "\n".join(tabla_t)
                + "\n\n#endif /* FILTROS_H_ */\n")
    return taps, max_taps


def graficar(carpeta, claves, res):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axs = plt.subplots(len(claves), 1, figsize=(9, 3.6 * len(claves)), squeeze=False)
    colores = ["#1f6fb4", "#e8710a", "#2e9e44", "#c62828", "#7b3fa0"]
    for ax, clave in zip(axs[:, 0], claves):
        cfg = DISENOS[clave]
        fmax = 8000
        if clave.startswith("EB"):
            fmax = 150
        elif clave == "PA":
            fmax = 150
        f = np.logspace(0, math.log10(fmax), 6000) if clave == "PBanda" else np.linspace(0.01, fmax, 6000)
        for c, fs in zip(colores, cfg.get("fss", FSS)):
            r = res[(clave, fs)]
            h = q15(r["h"]) / 32768
            g = 20 * np.log10(np.maximum(respuesta(h, f[f < fs / 2], fs), 1e-6))
            ax.plot(f[f < fs / 2], g, color=c, lw=1.2, label=f"{fs // 1000} kHz, N = {r['N']} (Q15)")
        spec = Spec(8000, cfg["bandas"](fmax * 2 if fmax * 2 > 8000 else 8000), cfg["ap"], cfg["ast"], cfg["par"])
        for f0, f1, d in spec.bandas:
            if d == 0:
                ax.add_patch(plt.Rectangle((f0, -cfg["ast"] + 1), min(f1, fmax) - f0, 40, color="#f4c7c3", lw=0))
            else:
                lim = 20 * math.log10(1 - spec.dp)
                ax.add_patch(plt.Rectangle((f0, -90), min(f1, fmax) - f0, 90 + lim, color="#f4c7c3", lw=0))
        if clave == "PBanda":
            ax.set_xscale("log")
            ax.set_xlim(1, fmax)
        else:
            ax.set_xlim(0, fmax)
        ax.set_ylim(-70, 5)
        ax.grid(alpha=0.3)
        ax.set_xlabel("frecuencia [Hz]")
        ax.set_ylabel("|H| [dB]")
        ax.set_title(cfg["titulo"] + "  (zonas rojas: plantilla con Astop de la consigna)", fontsize=9)
        ax.legend(fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig(os.path.join(AQUI, carpeta, "respuesta.png"), dpi=110)
    plt.close(fig)


def main():
    res = {}
    ruta_json = os.path.join(AQUI, "verificacion.json")
    previos = {}
    if "--reusar" in sys.argv and os.path.exists(ruta_json):
        # reusar los diseños ya calculados (lee los CSV) y solo regenerar header, gráficos y JSON
        for d in json.load(open(ruta_json, encoding="utf-8")):
            previos[(d["clave"], d["fs"])] = d
    for clave, cfg in DISENOS.items():
        os.makedirs(os.path.join(AQUI, cfg["carpeta"]), exist_ok=True)
        for fs in cfg.get("fss", FSS):
            spec = Spec(fs, cfg["bandas"](fs), cfg["ap"], cfg["ast"], cfg["par"])
            nombre = f"{clave}_{SUF[fs]}"
            csv = os.path.join(AQUI, cfg["carpeta"], nombre + ".csv")
            t = time.time()
            if (clave, fs) in previos and os.path.exists(csv):
                h = np.loadtxt(csv, delimiter=",", skiprows=1, usecols=1)
                N, metodo = len(h) - 1, previos[(clave, fs)]["metodo"]
            else:
                N, h, metodo = disenar(spec, cfg["N0"](fs))
                escribir_csv(csv, h)
            v = verificar(h, spec)
            # la Q15 se compara con lo que pide la consigna: solo Astop (sin el dB de margen). El Apass de 1 dB
            # es una elección de diseño; se informa el ripple medido y si quedó dentro de esa tolerancia.
            spec_c = Spec(fs, cfg["bandas"](fs), cfg["ap"], cfg["ast"] - 1, cfg["par"])
            vq = verificar(q15(h) / 32768, spec_c)
            vq["cumple_consigna"] = bool(vq["rechazo_db"] <= -(cfg["ast"] - 1) and vq["transicion_max_db"] <= 0.6)
            vq["dentro_de_apass"] = vq.pop("ok")
            res[(clave, fs)] = dict(clave=clave, fs=fs, N=N, taps=taps_cmsis(h), metodo=metodo, flotante=v,
                                    q15_vs_consigna=vq, h=h)
            print(f"{nombre:16s} N = {N:5d}  {metodo:22s} flotante {v}  Q15 {vq}  ({time.time() - t:.0f} s)",
                  flush=True)
    taps, max_taps = escribir_header(os.path.join(AQUI, "filtros.h"), res)
    for carpeta in sorted({c["carpeta"] for c in DISENOS.values()}):
        graficar(carpeta, [k for k, c in DISENOS.items() if c["carpeta"] == carpeta], res)
    with open(ruta_json, "w", encoding="utf-8") as f:
        json.dump([{k: v for k, v in r.items() if k != "h"} for r in res.values()], f, ensure_ascii=False, indent=1)
    print("filtros.h: MAX_TAPS =", max_taps)


if __name__ == "__main__":
    main()
