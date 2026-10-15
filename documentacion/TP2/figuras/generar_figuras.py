"""Genera las figuras SVG de teoria-tp2-filtros-fir.md y de los analisis-filtro-*.md.

Requiere numpy, scipy y matplotlib:
    pip install numpy scipy matplotlib
    python generar_figuras.py

Los filtros se diseñan con scipy (remez = Equiripple de filterDesigner,
firwin = método de ventanas). Las figuras de los análisis también leen los
coeficientes actuales de TP2/source/filtros.h para compararlos. Las figuras
se guardan en esta misma carpeta.
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal

AQUI = os.path.dirname(os.path.abspath(__file__))

# Paleta
AZUL, NARANJA, VERDE, ROJO, VIOLETA, GRIS = "#1f6fb4", "#e8710a", "#2e9e44", "#c62828", "#7b3fa0", "#6b6b6b"
PROHIBIDO = "#f4c7c3"

plt.rcParams.update({
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "savefig.facecolor": "white",
    "font.size": 10,
    "axes.titlesize": 11,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "svg.fonttype": "path",  # el texto viaja como trazos: se ve igual en cualquier visor
    "svg.hashsalt": "tp2",   # SVG reproducible entre corridas
})

# Especificación del pasa altos recomendado
FS = 8000
FSTOP, FPASS, APASS, ASTOP = 5, 35, 1.0, 31.0
ASTOP_CONSIGNA = 30.0
N_REC = 260


def deltas(ap, ast):
    g = 10 ** (ap / 20)
    return (g - 1) / (g + 1), 10 ** (-ast / 20)


DP, DS = deltas(APASS, ASTOP)
LIM_SUP_DB = 20 * np.log10(1 + DP)   # +0,486 dB
LIM_INF_DB = 20 * np.log10(1 - DP)   # -0,514 dB


def equiripple_pa(n, fs=FS, fst=FSTOP, fp=FPASS):
    return signal.remez(n + 1, [0, fst, fp, fs / 2], [0, 1], weight=[1 / DS, 1 / DP],
                        fs=fs, grid_density=32, maxiter=100)


def ventana_pa(n, fc, ventana, fs=FS):
    return signal.firwin(n + 1, fc, window=ventana, pass_zero=False, fs=fs, scale=True)


def resp_db(h, f, fs=FS):
    _, H = signal.freqz(h, worN=f, fs=fs)
    return 20 * np.log10(np.maximum(np.abs(H), 1e-12))


def coma(x, dec=1):
    """Número con coma decimal y signo menos tipográfico."""
    return f"{x:.{dec}f}".replace(".", ",").replace("-", "−")


def guardar(fig, nombre):
    fig.tight_layout()
    fig.savefig(os.path.join(AQUI, nombre), format="svg", metadata={"Date": None})
    plt.close(fig)
    print("ok", nombre)


H_REC = equiripple_pa(N_REC)


# ---------------------------------------------------------------- muestreo
def fig_aliasing():
    fs = 8000
    t = np.linspace(0, 2e-3, 2000)
    n = np.arange(0, 17)
    fig, ax = plt.subplots(figsize=(8, 3.2))
    ax.plot(t * 1e3, np.cos(2 * np.pi * 7000 * t), color=NARANJA, lw=1, label="7 kHz (por encima de fs/2)")
    ax.plot(t * 1e3, np.cos(2 * np.pi * 1000 * t), color=AZUL, lw=2, label="1 kHz")
    ax.plot(n / fs * 1e3, np.cos(2 * np.pi * 1000 * n / fs), "o", color="black", ms=6,
            label="muestras a fs = 8 kHz (iguales para las dos)")
    ax.set_xlabel("tiempo [ms]")
    ax.set_ylabel("amplitud")
    ax.set_title("Aliasing: muestreadas a 8 kHz, una senoidal de 7 kHz y una de 1 kHz son indistinguibles")
    ax.legend(loc="upper center", fontsize=8, ncol=3, framealpha=0.95)
    ax.set_ylim(-1.2, 1.75)
    guardar(fig, "muestreo_aliasing.svg")


def fig_misma_h_distinta_fs():
    f8 = np.logspace(0, np.log10(4000), 3000)
    f48 = np.logspace(0, np.log10(24000), 3000)
    fig, ax = plt.subplots(figsize=(8, 3.6))
    ax.semilogx(f8, resp_db(H_REC, f8, 8000), color=AZUL, lw=2, label="coeficientes de 8 kHz usados a 8 kHz")
    ax.semilogx(f48, resp_db(H_REC, f48, 48000), color=ROJO, lw=2, ls="--",
                label="los mismos coeficientes usados a 48 kHz")
    for f, c, txt in ((35, AZUL, "35 Hz"), (210, ROJO, "210 Hz = 35·6")):
        ax.axvline(f, color=c, lw=1, ls=":")
        ax.text(f * 1.08, -45, txt, color=c, fontsize=9)
    ax.set_xlim(1, 24000)
    ax.set_ylim(-60, 5)
    ax.set_xlabel("frecuencia [Hz] (escala logarítmica)")
    ax.set_ylabel("|H| [dB]")
    ax.set_title("Los coeficientes fijan la respuesta en ω: cambiar fs corre el filtro en Hz")
    ax.legend(loc="lower right", fontsize=8, framealpha=0.95)
    guardar(fig, "misma_h_distinta_fs.svg")


def fig_zoh():
    fs, f0 = 8000, 1000
    t = np.linspace(0, 2e-3, 4000)
    n = np.arange(0, 17)
    x = np.sin(2 * np.pi * f0 * n / fs)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.2), gridspec_kw={"width_ratios": [1.3, 1]})
    a1.plot(t * 1e3, np.sin(2 * np.pi * f0 * t), color=GRIS, lw=1, label="señal ideal")
    a1.step(n / fs * 1e3, x, where="post", color=AZUL, lw=2, label="salida del DAC (ZOH)")
    a1.set_xlabel("tiempo [ms]")
    a1.set_title("Retención de orden cero: 1 kHz a fs = 8 kHz")
    a1.legend(fontsize=8, loc="lower left", framealpha=0.95)
    r = np.linspace(1e-4, 0.5, 400)
    a2.plot(r, 20 * np.log10(np.sinc(r)), color=NARANJA, lw=2)
    for v in (0.25, 0.5):
        a2.plot(v, 20 * np.log10(np.sinc(v)), "o", color=NARANJA)
        a2.annotate(f"{coma(20 * np.log10(np.sinc(v)))} dB", (v, 20 * np.log10(np.sinc(v))),
                    textcoords="offset points", xytext=(-55, -4), fontsize=9)
    a2.set_xlabel("f / fs")
    a2.set_ylabel("dB")
    a2.set_title("Caída sinc de la ZOH")
    guardar(fig, "dac_zoh.svg")


# ---------------------------------------------------------------- especificación
def fig_plantilla():
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 4))
    f = np.linspace(0, 100, 4000)
    a1.add_patch(plt.Rectangle((0, -ASTOP_CONSIGNA), FSTOP, 40, color=PROHIBIDO, lw=0))
    a1.add_patch(plt.Rectangle((FPASS, -80), 100 - FPASS, 80 + LIM_INF_DB, color=PROHIBIDO, lw=0))
    a1.plot(f, resp_db(H_REC, f), color=AZUL, lw=2, label=f"Equiripple N = {N_REC}")
    a1.axhline(-ASTOP_CONSIGNA, color=ROJO, lw=1, ls="--")
    a1.text(52, -ASTOP_CONSIGNA - 4, "−30 dB (Astop de la consigna)", color=ROJO, fontsize=8)
    a1.axvline(FSTOP, color=GRIS, lw=1, ls=":")
    a1.axvline(FPASS, color=GRIS, lw=1, ls=":")
    a1.annotate("", xy=(FPASS, -50), xytext=(FSTOP, -50), arrowprops=dict(arrowstyle="<->", color="black"))
    a1.text((FSTOP + FPASS) / 2, -48, "transición\nΔf = 30 Hz", ha="center", fontsize=8)
    a1.text(FSTOP + 0.5, -62, "Fstop\n5 Hz", fontsize=8)
    a1.text(FPASS + 0.5, -62, "Fpass\n35 Hz", fontsize=8)
    a1.text(1, 2, "rechazo", fontsize=8, color=ROJO)
    a1.text(60, -12, "zona prohibida", fontsize=8, color=ROJO)
    a1.set_xlim(0, 100)
    a1.set_ylim(-70, 6)
    a1.set_xlabel("frecuencia [Hz]")
    a1.set_ylabel("|H| [dB]")
    a1.set_title("(a) Plantilla y respuesta, 0–100 Hz")
    a1.legend(loc="lower right", fontsize=8, framealpha=0.95)

    f2 = np.linspace(FPASS, FS / 2, 6000)
    a2.add_patch(plt.Rectangle((FPASS, LIM_SUP_DB), FS, 2, color=PROHIBIDO, lw=0))
    a2.add_patch(plt.Rectangle((FPASS, -2), FS, 2 + LIM_INF_DB, color=PROHIBIDO, lw=0))
    a2.plot(f2, resp_db(H_REC, f2), color=AZUL, lw=1)
    a2.set_xlim(0, FS / 2)
    a2.set_ylim(-1, 1)
    a2.set_xlabel("frecuencia [Hz]")
    a2.set_title("(b) Banda de paso: ripple parejo dentro de Apass = 1 dB")
    a2.text(200, 0.62, "+0,49 dB", fontsize=8, color=ROJO)
    a2.text(200, -0.72, "−0,51 dB", fontsize=8, color=ROJO)
    guardar(fig, "plantilla_pasa_altos.svg")


def fig_orden_vs_parametros():
    from matplotlib.ticker import FixedLocator, ScalarFormatter
    fig, ((a1, a2), (a3, a4)) = plt.subplots(2, 2, figsize=(10, 6.4))
    df = [5, 10, 15, 20, 25, 30]
    datos = {
        "Equiripple": ([1910, 910, 648, 456, 424, 260], AZUL),
        "Least-squares": ([3028, 1464, 994, 766, 558, 352], VERDE),
        "Chebyshev 30 dB": ([3078, 1504, 1028, 790, 608, 512], VIOLETA),
        "Hamming": ([4032, 2024, 1352, 1002, 804, 666], NARANJA),
        "Blackman": ([5338, 2670, 1780, 1336, 1068, 892], ROJO),
    }
    for nombre, (n, c) in datos.items():
        a1.plot(df, n, "o-", color=c, lw=2.2 if nombre == "Equiripple" else 1, ms=4, label=nombre)
    a1.set_yscale("log")
    a1.yaxis.set_major_locator(FixedLocator([250, 500, 1000, 2000, 5000]))
    a1.yaxis.set_major_formatter(ScalarFormatter())
    a1.yaxis.set_minor_locator(FixedLocator([]))
    a1.set_xlabel("Δf = Fpass − Fstop [Hz]")
    a1.set_ylabel("orden N")
    a1.set_title("(a) Ancho de transición (Apass 1, Astop 30)")
    a1.legend(fontsize=7, framealpha=0.95)

    a2.plot([0.1, 0.5, 1, 2, 3], [634, 490, 424, 354, 310], "o-", color=AZUL, lw=2)
    a2.set_xlabel("Apass [dB]")
    a2.set_ylabel("orden N")
    a2.set_title("(b) Apass (Equiripple, Fstop 10, Astop 30)")

    a3.plot([30, 31, 33, 40], [424, 428, 434, 444], "o-", color=NARANJA, lw=2)
    a3.set_xlabel("Astop [dB]")
    a3.set_ylabel("orden N")
    a3.set_title("(c) Astop (Equiripple, Fstop 10, Apass 1)")
    for a in (a2, a3):
        a.set_ylim(250, 680)

    fs = np.array([8, 16, 22, 44, 48])
    n = np.array([260, 518, 716, 1430, 1560])
    a4.plot(fs, fs / 8 * 260, ":", color=GRIS, label="260 · fs / 8 kHz (proporcional)")
    a4.plot(fs, n, "o", color=AZUL, ms=6, label="Equiripple recomendado")
    a4.set_xlabel("fs [kHz]")
    a4.set_ylabel("orden N")
    a4.set_title("(d) Frecuencia de muestreo (Fstop 5, Astop 31)")
    a4.legend(fontsize=8, framealpha=0.95)
    fig.suptitle("Orden mínimo del pasa altos (Fpass = 35 Hz) según cada parámetro, fs = 8 kHz salvo (d)", fontsize=11)
    guardar(fig, "orden_vs_parametros.svg")


# ---------------------------------------------------------------- diseño
def fig_gibbs():
    fs, fc = 8000, 1000
    f = np.linspace(0, 2000, 4000)
    fig, ax = plt.subplots(figsize=(8, 3.4))
    for n, c in ((40, NARANJA), (160, VERDE), (640, AZUL)):
        h = ventana_pa(n, fc, "boxcar", fs)
        _, H = signal.freqz(h, worN=f, fs=fs)
        ax.plot(f, np.abs(H), color=c, lw=1.5, label=f"ideal truncado, N = {n}")
    ax.axvline(fc, color=GRIS, ls=":", lw=1)
    ax.axhline(1.09, color=ROJO, ls="--", lw=1)
    ax.text(80, 1.11, "sobrepico ≈ 9 %: no baja al aumentar N, solo se angosta", color=ROJO, fontsize=8)
    ax.set_xlim(0, 2000)
    ax.set_ylim(0, 1.25)
    ax.set_xlabel("frecuencia [Hz] (pasa altos con corte en 1 kHz, fs = 8 kHz)")
    ax.set_ylabel("|H| (lineal)")
    ax.set_title("Efecto Gibbs: truncar la respuesta ideal (ventana rectangular)")
    ax.legend(loc="lower right", fontsize=8, framealpha=0.95)
    guardar(fig, "gibbs.svg")


VENTANAS = [
    ("Rectangular", "boxcar", GRIS),
    ("Bartlett", "bartlett", VIOLETA),
    ("Hann", "hann", VERDE),
    ("Hamming", "hamming", NARANJA),
    ("Blackman", "blackman", ROJO),
    ("Kaiser β = 2,12", ("kaiser", 2.12), AZUL),
]


def fig_ventanas():
    L = 101
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.8))
    for nombre, w, c in VENTANAS:
        v = signal.get_window(w, L, fftbins=False)
        a1.plot(np.arange(L), v, color=c, lw=1.5, label=nombre)
        W = np.fft.rfft(v, 64 * L)
        Wdb = 20 * np.log10(np.maximum(np.abs(W) / np.abs(W[0]), 1e-12))
        x = np.arange(len(W)) / (64 * L) * L  # en múltiplos de fs/L
        a2.plot(x, Wdb, color=c, lw=1.2, label=nombre)
    a1.set_xlabel("n")
    a1.set_ylabel("w[n]")
    a1.set_title("(a) Ventanas en el tiempo (L = 101)")
    a1.legend(fontsize=7, framealpha=0.95)
    a2.set_xlim(0, 8)
    a2.set_ylim(-100, 3)
    a2.set_xlabel("frecuencia [múltiplos de fs/L]")
    a2.set_ylabel("dB")
    a2.set_title("(b) Espectro normalizado de cada ventana")
    a2.text(0.15, -96, "lóbulo principal → ancho de transición\nlóbulos laterales → atenuación", fontsize=8)
    guardar(fig, "ventanas.svg")


def fig_metodos_mismo_orden():
    n = N_REC
    f = np.linspace(0, 100, 4000)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 4))
    a1.add_patch(plt.Rectangle((0, -ASTOP_CONSIGNA), FSTOP, 40, color=PROHIBIDO, lw=0))
    a1.add_patch(plt.Rectangle((FPASS, -80), 100 - FPASS, 80 + LIM_INF_DB, color=PROHIBIDO, lw=0))
    fc = (FSTOP + FPASS) / 2
    curvas = [("Equiripple", H_REC, AZUL, 2.2)]
    for nombre, w, c in VENTANAS:
        if nombre in ("Rectangular", "Hamming", "Blackman", "Kaiser β = 2,12"):
            curvas.append((nombre, ventana_pa(n, fc, w), c, 1.2))
    f2 = np.linspace(FPASS, 400, 3000)
    for nombre, h, c, lw in curvas:
        a1.plot(f, resp_db(h, f), color=c, lw=lw, label=nombre)
        a2.plot(f2, resp_db(h, f2), color=c, lw=lw, label=nombre)
    a1.set_xlim(0, 100)
    a1.set_ylim(-70, 6)
    a1.set_xlabel("frecuencia [Hz]")
    a1.set_ylabel("|H| [dB]")
    a1.set_title(f"(a) Mismo orden N = {n}, fs = 8 kHz")
    a1.legend(loc="lower right", fontsize=7, framealpha=0.95)
    a2.add_patch(plt.Rectangle((FPASS, -6), 400, 6 + LIM_INF_DB, color=PROHIBIDO, lw=0))
    a2.set_xlim(FPASS, 400)
    a2.set_ylim(-6, 1)
    a2.set_xlabel("frecuencia [Hz]")
    a2.set_title("(b) Arranque de la banda de paso")
    a2.text(120, -4.6, "Equiripple usa todo el ripple permitido;\nlas ventanas quedan por debajo de −0,5 dB\ncerca de 35 Hz", fontsize=8)
    guardar(fig, "metodos_mismo_orden.svg")


def fig_respuesta_impulso():
    n = np.arange(len(H_REC))
    M = N_REC // 2
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.6))
    a1.vlines(n, 0, H_REC, color=AZUL, lw=0.8)
    a1.plot(n, H_REC, "o", color=AZUL, ms=1.5)
    a1.annotate(f"h[{M}] = {coma(H_REC[M], 4)}", (M, H_REC[M]), textcoords="offset points",
                xytext=(12, -8), fontsize=9)
    a1.set_xlabel("n")
    a1.set_ylabel("h[n]")
    a1.set_title(f"(a) Respuesta al impulso, N = {N_REC} ({len(H_REC)} coeficientes)")
    a2.vlines(n, 0, H_REC, color=AZUL, lw=0.8)
    a2.plot(n, H_REC, "o", color=AZUL, ms=1.5)
    a2.axvline(M, color=ROJO, ls="--", lw=1)
    a2.text(M + 3, 0.0045, "eje de simetría\nh[k] = h[N−k]", color=ROJO, fontsize=8)
    a2.set_ylim(-0.0075, 0.006)
    a2.set_xlabel("n")
    a2.set_title("(b) Zoom: el resto son chicos y negativos")
    a2.text(8, -0.0072, f"los extremos se salen de escala: h[0] = h[N] = {coma(H_REC[0], 4)}", fontsize=8)
    guardar(fig, "respuesta_impulso_pa.svg")


def ls_simetrico(L, fs=FS):
    """FIR simétrico de L coeficientes por mínimos cuadrados ponderados (L impar: tipo I, par: tipo II)."""
    mitad, c = L // 2, (L - 1) / 2
    f = np.r_[np.linspace(0, FSTOP, 300), np.linspace(FPASS, fs / 2, 6000)]
    w = 2 * np.pi * f / fs
    deseada = np.r_[np.zeros(300), np.ones(6000)]
    peso = np.r_[np.full(300, 1 / DS), np.full(6000, 1 / DP)]
    A = 2 * np.cos(np.outer(w, np.arange(mitad) - c))
    if L % 2:
        A = np.c_[A, np.ones(len(w))]  # coeficiente central
    x = np.linalg.lstsq(A * peso[:, None], deseada * peso, rcond=None)[0]
    centro = [x[mitad]] if L % 2 else []
    return np.r_[x[:mitad], centro, x[:mitad][::-1]]


def fig_tipos_fir():
    h1, h2 = ls_simetrico(N_REC + 1), ls_simetrico(N_REC + 2)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.6), gridspec_kw={"width_ratios": [1.4, 1]})
    f = np.linspace(0, FS / 2, 6000)
    f2 = np.linspace(3800, FS / 2, 2000)
    for a, ff in ((a1, f), (a2, f2)):
        a.plot(ff, resp_db(h1, ff), color=AZUL, lw=2, label=f"tipo I (N = {N_REC}, par)")
        a.plot(ff, resp_db(h2, ff), color=ROJO, lw=1.5, ls="--", label=f"tipo II (N = {N_REC + 1}, impar)")
        a.set_ylim(-40, 5)
        a.set_xlabel("frecuencia [Hz]")
    a1.set_xlim(0, FS / 2)
    a1.set_ylabel("|H| [dB]")
    a1.set_title("(a) Mismo diseño, orden par e impar")
    a1.legend(loc="lower center", fontsize=8, framealpha=0.95)
    a2.set_xlim(3800, FS / 2)
    a2.set_title("(b) Zoom cerca de fs/2 = 4000 Hz")
    a2.annotate("cero forzado en fs/2:\nel tipo II no puede ser pasa altos", (FS / 2, -38), textcoords="offset points",
                xytext=(-170, 40), arrowprops=dict(arrowstyle="->", color=ROJO), color=ROJO, fontsize=8)
    guardar(fig, "tipos_fir_pasa_altos.svg")


# ---------------------------------------------------------------- comportamiento en el tiempo
def fig_tiempo():
    t = np.arange(0, int(0.6 * FS)) / FS
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 5.6))
    x = 0.3 + 0.4 * np.sin(2 * np.pi * 100 * t)
    y = signal.lfilter(H_REC, 1, x)
    sel = t < 0.12
    a1.plot(t[sel] * 1e3, x[sel], color=GRIS, lw=1.2, label="entrada: 100 Hz + continua")
    a1.plot(t[sel] * 1e3, y[sel], color=AZUL, lw=1.5, label="salida del pasa altos")
    tg = N_REC / 2 / FS * 1e3
    a1.axvline(tg, color=ROJO, ls="--", lw=1)
    a1.text(tg + 1, 0.85, f"τg = N/(2·fs) = {coma(tg, 2)} ms", color=ROJO, fontsize=8)
    a1.axvspan(0, N_REC / FS * 1e3, color="#fff3cd", alpha=0.6, lw=0)
    a1.text(1, -0.6, "transitorio: la línea\nde retardos se llena", fontsize=8)
    a1.set_xlabel("tiempo [ms]")
    a1.set_title("(a) Senoidal en la banda de paso: pasa entera y retrasada, sin la continua")
    a1.legend(loc="upper right", fontsize=8, framealpha=0.95)
    a1.set_ylim(-0.8, 1.05)

    x2 = 0.4 * signal.square(2 * np.pi * 5 * t)
    y2 = signal.lfilter(H_REC, 1, x2)
    a2.plot(t * 1e3, x2, color=GRIS, lw=1.2, label="entrada: cuadrada de 5 Hz, ±0,4")
    a2.plot(t * 1e3, y2, color=AZUL, lw=1.2, label="salida del pasa altos")
    for v in (0.4, -0.4):
        a2.axhline(v, color=ROJO, ls=":", lw=1)
    a2.text(5, 0.6, "fase lineal: la mitad del salto aparece antes del flanco (pre-oscilación) y la mitad "
            "después → picos ≈ ±0,4", color=ROJO, fontsize=8)
    a2.set_xlabel("tiempo [ms]")
    a2.set_title("(b) Cuadrada de baja frecuencia: queda solo lo que cambia rápido (los flancos)")
    a2.legend(loc="lower right", fontsize=8, framealpha=0.95)
    a2.set_ylim(-0.8, 0.8)
    guardar(fig, "respuesta_temporal_pa.svg")


# ---------------------------------------------------------------- tiempo real
def fig_ciclos():
    fs = np.array([8000, 16000, 22000, 44000, 48000])
    disp = 150e6 / fs
    taps_rec = np.array([262, 520, 718, 1432, 1562])
    taps_act = np.array([822, 1642, 2256, 4508, 4918])
    series = [
        ("disponibles (150 MHz / fs)", disp, "#bdbdbd"),
        ("actual del repo + arm_fir_q15", 9 * taps_act + 200, ROJO),
        ("recomendado + arm_fir_q15", 9 * taps_rec + 200, AZUL),
        ("recomendado + FIR optimizado", 2 * taps_rec + 200, VERDE),
    ]
    fig, ax = plt.subplots(figsize=(9, 3.8))
    x = np.arange(len(fs))
    w = 0.2
    for i, (nombre, v, c) in enumerate(series):
        ax.bar(x + (i - 1.5) * w, v, w, color=c, label=nombre)
    ax.set_yscale("log")
    ax.set_xticks(x, [f"{f // 1000} kHz" for f in fs])
    ax.set_ylabel("ciclos por muestra")
    ax.set_title("Presupuesto de ciclos: si la barra de color supera a la gris, no entra (estimado, medir con DWT)")
    ax.legend(fontsize=8, ncol=2, loc="upper left", framealpha=0.95)
    ax.set_ylim(300, 3e5)
    guardar(fig, "presupuesto_ciclos.svg")


# ---------------------------------------------------------------- herramientas básicas
def fig_decibeles():
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.8), gridspec_kw={"width_ratios": [1, 1.2]})
    db = np.linspace(-60, 10, 500)
    a1.semilogy(db, 10 ** (db / 20), color=AZUL, lw=2)
    for d in (6, 0, -3, -6, -10, -20, -30, -40):
        k = 10 ** (d / 20)
        a1.plot(d, k, "o", color=NARANJA, ms=5)
        txt = f"× {coma(k, 3 if k < 0.1 else 2)}"
        a1.annotate(f"{coma(d, 0)} dB → {txt}", (d, k), textcoords="offset points", xytext=(6, -3), fontsize=8)
    a1.set_xlim(-60, 15)
    a1.set_xlabel("ganancia [dB]")
    a1.set_ylabel("factor de amplitud (escala log)")
    a1.set_title("(a) De dB a \"veces\": factor = 10^(dB/20)")

    t = np.linspace(0, 2e-3, 1000)
    a2.plot(t * 1e3, 1.0 * np.sin(2 * np.pi * 1000 * t), color=GRIS, lw=1.5, label="entrada: 2 Vpp")
    k30 = 10 ** (-30 / 20)
    a2.plot(t * 1e3, k30 * np.sin(2 * np.pi * 1000 * t), color=ROJO, lw=2,
            label=f"salida a −30 dB: 2 × {coma(k30, 4)} = {coma(2000 * k30, 0)} mVpp")
    a2.set_ylim(-1.3, 1.5)
    a2.set_xlabel("tiempo [ms]")
    a2.set_ylabel("tensión [V]")
    a2.set_title("(b) Misma escala: así de chica es una señal a −30 dB")
    a2.legend(loc="upper right", fontsize=8, framealpha=0.95)
    guardar(fig, "decibeles.svg")


def fig_cuantizacion():
    B = 3  # pocos bits a propósito, para que se vean los escalones
    q = 2 / 2 ** B
    t = np.linspace(0, 1, 2000)
    x = 0.8 * np.sin(2 * np.pi * t)  # sin llegar al último escalón: solo error de redondeo
    xq = np.clip(np.round(x / q) * q, -1, 1 - q)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.4))
    a1.plot(t, x, color=GRIS, lw=1.2, label="señal analógica")
    a1.plot(t, xq, color=AZUL, lw=2, label=f"cuantizada con {B} bits ({2 ** B} niveles)")
    for k in range(-2 ** (B - 1), 2 ** (B - 1)):
        a1.axhline(k * q, color="#dddddd", lw=0.6, zorder=0)
    a1.set_xlabel("tiempo (un período)")
    a1.set_title("(a) Cada muestra se redondea al escalón más cercano")
    a1.legend(fontsize=8, loc="lower left", framealpha=0.95)
    a2.plot(t, x - xq, color=ROJO, lw=1)
    a2.axhline(q / 2, color=GRIS, ls="--", lw=1)
    a2.axhline(-q / 2, color=GRIS, ls="--", lw=1)
    a2.text(0.01, q / 2 * 1.08, "+½ escalón", fontsize=8)
    a2.text(0.01, -q / 2 * 1.25, "−½ escalón", fontsize=8)
    a2.set_ylim(-q * 0.8, q * 0.8)
    a2.set_xlabel("tiempo (un período)")
    a2.set_title("(b) Error = señal − cuantizada: parece ruido")
    guardar(fig, "cuantizacion_error.svg")


def fig_centrado():
    t = np.linspace(0, 2, 1000)
    v = 1.65 + 1.2 * np.sin(2 * np.pi * t)        # tensión con offset, ejemplo con Vref = 3,3 V
    code = np.round(v / 3.3 * 65535)               # ADC de 16 bits, sin signo
    sin_restar = ((code + 32768) % 65536) - 32768  # lo que pasa si se lo mete directo en un int16
    q15 = code - 32768
    fig, axs = plt.subplots(2, 2, figsize=(10, 6))
    (a1, a2), (a3, a4) = axs
    a1.plot(t, v, color=AZUL, lw=2)
    a1.axhline(1.65, color=GRIS, ls="--", lw=1)
    a1.set_ylim(0, 3.3)
    a1.set_ylabel("V")
    a1.set_title("(a) Entrada del ADC, montada en Vref/2 (ej. 3,3 V)")
    a2.plot(t, code, color=AZUL, lw=2)
    a2.axhline(32768, color=GRIS, ls="--", lw=1)
    a2.axhline(32767, color=ROJO, ls=":", lw=1.2)
    a2.text(0.02, 34500, "32767 = máximo de un int16", color=ROJO, fontsize=8)
    a2.set_ylim(0, 65535)
    a2.set_title("(b) convValue: 0 … 65535, sin signo, centro en 32768")
    a3.plot(t, sin_restar, color=ROJO, lw=1.5)
    a3.set_ylim(-32768, 32767)
    a3.set_title("(c) ERROR: meterlo en un int16 sin restar 32768")
    a3.text(0.52, -28000, "lo que pasa de 32767 \"da la vuelta\" a negativo", color=ROJO, fontsize=8)
    a4.plot(t, q15, color=VERDE, lw=2)
    a4.axhline(0, color=GRIS, ls="--", lw=1)
    a4.set_ylim(-32768, 32767)
    a4.set_title("(d) convValue − 32768 = Q15, centrada en 0")
    for a in (a3, a4):
        a.set_xlabel("tiempo (períodos)")
    guardar(fig, "centrado_q15.svg")


def fig_filtros_2_coef():
    f = np.linspace(0, 4000, 400)
    w = 2 * np.pi * f / 8000
    fig, ax = plt.subplots(figsize=(8, 3.4))
    ax.plot(f, np.abs(np.cos(w / 2)), color=AZUL, lw=2, label="h = [0,5 ; 0,5] (promedio de 2): pasa bajos")
    ax.plot(f, np.abs(np.sin(w / 2)), color=ROJO, lw=2, label="h = [0,5 ; −0,5] (diferencia): pasa altos")
    for fx in (0, 2000, 4000):
        for g, c in ((abs(np.cos(np.pi * fx / 8000)), AZUL), (abs(np.sin(np.pi * fx / 8000)), ROJO)):
            ax.plot(fx, g, "o", color=c)
    ax.text(2050, 0.74, "0,707 = −3 dB", fontsize=8)
    ax.set_xlim(0, 4000)
    ax.set_ylim(0, 1.15)
    ax.set_xlabel("frecuencia [Hz] (fs = 8 kHz)")
    ax.set_ylabel("|H| (lineal)")
    ax.set_title("Los filtros FIR más simples posibles: 2 coeficientes")
    ax.legend(loc="upper center", fontsize=8, ncol=1, framealpha=0.95)
    guardar(fig, "filtros_2_coeficientes.svg")


# ---------------------------------------------------------------- análisis de PB, PBanda y EB
FILTROS_H = os.path.join(AQUI, "..", "..", "..", "TP2", "source", "filtros.h")


def remez_bandas(n, fs, bandas, ap=1.0, ast=31.0):
    """Equiripple para bandas [(f0, f1, deseado)], con pesos 1/delta como filterDesigner."""
    dp, ds = deltas(ap, ast)
    bordes, des, pesos = [], [], []
    for f0, f1, d in bandas:
        bordes += [f0, f1]
        des.append(d)
        pesos.append(1 / (dp if d == 1 else ds))
    return signal.remez(n + 1, bordes, des, weight=pesos, fs=fs, grid_density=32, maxiter=200)


def coefs_repo(nombre):
    """Coeficientes Q15 del filtro `nombre` de filtros.h, en el orden natural h[0..N]."""
    import re
    texto = open(FILTROS_H, encoding="utf-8").read()
    m = re.search(r"%s\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}" % nombre, texto)
    return (np.array([int(x) for x in re.findall(r"-?\d+", m.group(1))]) / 32768)[::-1]


def sombrear(ax, bandas, fs, ap=1.0, ast=30.0, ymin=-80, ymax=6):
    """Zonas prohibidas de la plantilla: arriba de −Astop en rechazo, abajo de −Apass/2 en paso."""
    dp, _ = deltas(ap, ast)
    for f0, f1, d in bandas:
        if d == 0:
            ax.add_patch(plt.Rectangle((f0, -ast), f1 - f0, ymax + ast, color=PROHIBIDO, lw=0))
        else:
            lim = 20 * np.log10(1 - dp)
            ax.add_patch(plt.Rectangle((f0, ymin), f1 - f0, lim - ymin, color=PROHIBIDO, lw=0))


def fig_pb_analisis():
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10.5, 4))
    fs = 8000
    f = np.linspace(3000, 4000, 3000)
    sombrear(a1, [(0, 3600, 1), (3900, 4000, 0)], fs)
    curvas = [
        ("recomendado: Equiripple 3600/3900, N = 33", remez_bandas(33, fs, [(0, 3600, 1), (3900, 4000, 0)]), AZUL, 2.2),
        ("lectura B: Equiripple 3500/3700, N = 48", remez_bandas(48, fs, [(0, 3500, 1), (3700, 4000, 0)]), NARANJA, 1.4),
        ("actual del repo (Kaiser), N = 62", coefs_repo("PB_8k"), ROJO, 1.2),
    ]
    for nombre, h, c, lw in curvas:
        a1.plot(f, resp_db(h, f, fs), color=c, lw=lw, label=nombre)
    a1.axhline(-30, color=ROJO, ls="--", lw=1)
    a1.annotate("el actual no llega a −30 dB", (3760, -28.3), textcoords="offset points", xytext=(-150, 30),
                arrowprops=dict(arrowstyle="->", color=ROJO), color=ROJO, fontsize=8)
    a1.set_xlim(3000, 4000)
    a1.set_ylim(-70, 6)
    a1.set_xlabel("frecuencia [Hz]")
    a1.set_ylabel("|H| [dB]")
    a1.set_title("(a) fs = 8 kHz: la banda de rechazo es solo 3900–4000 Hz")
    a1.legend(loc="lower left", fontsize=7, framealpha=0.95)

    fs = 48000
    f = np.linspace(0, 10000, 4000)
    sombrear(a2, [(0, 3600, 1), (3900, 10000, 0)], fs)
    for nombre, fst, n, c in (("Fstop 3900, N = 189", 3900, 189, AZUL), ("Fstop 4000, N = 145", 4000, 145, VERDE),
                              ("Fstop 7200, N = 16", 7200, 16, VIOLETA)):
        h = remez_bandas(n, fs, [(0, 3600, 1), (fst, fs / 2, 0)])
        a2.plot(f, resp_db(h, f, fs), color=c, lw=1.6, label=nombre)
    a2.axhline(-30, color=ROJO, ls="--", lw=1)
    a2.set_xlim(0, 10000)
    a2.set_ylim(-70, 6)
    a2.set_xlabel("frecuencia [Hz]")
    a2.set_title("(b) fs = 48 kHz: el Fstop elegido cambia mucho el costo")
    a2.legend(loc="lower left", fontsize=8, framealpha=0.95)
    a2.text(5200, -66, "zona roja: plantilla de Fstop = 3900", fontsize=8, color=ROJO,
            bbox=dict(facecolor="white", edgecolor="none", alpha=0.9))
    guardar(fig, "analisis_pb.svg")


def fig_pbanda_analisis():
    fs = 8000
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10.5, 4))
    # (a) borde inferior: igual que el pasa altos
    bandas_rec = [(0, 5, 0), (35, 3500, 1), (3560, 4000, 0)]
    h_rec = remez_bandas(260, fs, bandas_rec)
    f = np.linspace(0, 100, 3000)
    sombrear(a1, [(0, 5, 0), (35, 100, 1)], fs)
    a1.plot(f, resp_db(h_rec, f, fs), color=AZUL, lw=2.2, label="recomendado: N = 260")
    a1.plot(f, resp_db(coefs_repo("PBanda_8k"), f, fs), color=ROJO, lw=1.2, label="actual del repo (Kaiser), N = 820")
    a1.set_xlim(0, 100)
    a1.set_ylim(-70, 6)
    a1.set_xlabel("frecuencia [Hz]")
    a1.set_ylabel("|H| [dB]")
    a1.set_title("(a) Borde inferior (35 Hz): igual que el pasa altos")
    a1.legend(loc="lower right", fontsize=8, framealpha=0.95)
    # (b) borde superior: el "bulto" de Equiripple cuando la transición es muy ancha
    f = np.linspace(3300, 4000, 3000)
    h_ingenuo = remez_bandas(260, fs, [(0, 5, 0), (35, 3500, 1), (3700, 4000, 0)])
    sombrear(a2, [(0, 3500, 1), (3560, 4000, 0)], fs, ymax=70)
    a2.plot(f, resp_db(h_ingenuo, f, fs), color=ROJO, lw=1.6, label="Fstop2 = 3700 (transición de 200 Hz)")
    a2.plot(f, resp_db(h_rec, f, fs), color=AZUL, lw=2.2, label="Fstop2 = 3560 (transición de 60 Hz)")
    a2.annotate("+56 dB: amplifica ~600 veces\nlo que caiga entre 3500 y 3700 Hz", (3610, 55), textcoords="offset points",
                xytext=(20, -20), color=ROJO, fontsize=8)
    a2.set_xlim(3300, 4000)
    a2.set_ylim(-70, 70)
    a2.set_xlabel("frecuencia [Hz]")
    a2.set_title("(b) Borde superior: el \"bulto\" de Equiripple, ambos con N = 260")
    a2.legend(loc="lower left", fontsize=8, framealpha=0.95)
    guardar(fig, "analisis_pbanda.svg")


def fig_eb_analisis():
    fs = 8000
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(10, 7.2))
    f = np.linspace(0, 120, 4000)
    sombrear(a1, [(42.5, 57.5, 0)], fs, ast=25)
    eb = lambda fp1, fp2, n: remez_bandas(n, fs, [(0, fp1, 1), (42.5, 57.5, 0), (fp2, fs / 2, 1)], ast=26)
    h_rec = eb(35, 65, 1108)
    curvas = [
        ("Equiripple 35/65, N = 1108 (recomendado)", h_rec, AZUL, 2.2, "-"),
        ("Equiripple 20/80, N = 450 (versión liviana)", eb(20, 80, 450), VERDE, 1.6, "-"),
        ("actual del repo (Kaiser 35/65), N = 1268", coefs_repo("EB_8k"), ROJO, 1.0, "-"),
    ]
    for nombre, h, c, lw, ls in curvas:
        a1.plot(f, resp_db(h, f, fs), color=c, lw=lw, ls=ls, label=nombre)
    b, a = signal.iirnotch(50, 50 / 15, fs=fs)
    _, H = signal.freqz(b, a, worN=f, fs=fs)
    a1.plot(f, 20 * np.log10(np.maximum(np.abs(H), 1e-12)), color=VIOLETA, lw=1.6, ls="--",
            label="IIR notch de 2.º orden (5 coeficientes), como referencia")
    a1.axhline(-3, color=GRIS, ls=":", lw=1)
    a1.set_xlim(0, 120)
    a1.set_ylim(-60, 4)
    a1.set_xlabel("frecuencia [Hz]")
    a1.set_ylabel("|H| [dB]")
    a1.set_title("(a) fs = 8 kHz: la zona roja es la banda de rechazo de la consigna (42,5–57,5 Hz, 25 dB)")
    a1.legend(loc="lower left", fontsize=8, framealpha=0.95)

    t = np.arange(0, int(0.5 * fs)) / fs
    x = 0.5 * np.sin(2 * np.pi * 10 * t) + 0.25 * np.sin(2 * np.pi * 50 * t)
    y = signal.lfilter(h_rec, 1, x)
    tg = 1108 / 2 / fs
    a2.plot(t * 1e3, x, color=GRIS, lw=0.9, label="entrada: señal de 10 Hz + zumbido de 50 Hz")
    a2.plot(t * 1e3, y, color=AZUL, lw=1.4, label="salida del elimina banda")
    a2.plot(t * 1e3, 0.5 * np.sin(2 * np.pi * 10 * (t - tg)), color=NARANJA, lw=1.2, ls="--",
            label="10 Hz sola, retrasada τg (lo que debería quedar)")
    a2.axvspan(0, 1108 / fs * 1e3, color="#fff3cd", alpha=0.6, lw=0)
    a2.text(5, -0.95, f"transitorio: N/fs = {coma(1108 / fs * 1e3, 0)} ms", fontsize=8)
    a2.set_xlabel("tiempo [ms]")
    a2.set_ylim(-1.05, 1.05)
    a2.set_title(f"(b) Simulación: después del transitorio el zumbido desaparece; la señal sale retrasada τg = {coma(tg * 1e3, 1)} ms")
    a2.legend(loc="upper right", fontsize=8, framealpha=0.95)
    guardar(fig, "analisis_eb.svg")


if __name__ == "__main__":
    fig_eb_analisis()
    fig_pbanda_analisis()
    fig_pb_analisis()
    fig_decibeles()
    fig_cuantizacion()
    fig_centrado()
    fig_filtros_2_coef()
    fig_aliasing()
    fig_misma_h_distinta_fs()
    fig_zoh()
    fig_plantilla()
    fig_orden_vs_parametros()
    fig_gibbs()
    fig_ventanas()
    fig_metodos_mismo_orden()
    fig_respuesta_impulso()
    fig_tipos_fir()
    fig_tiempo()
    fig_ciclos()
