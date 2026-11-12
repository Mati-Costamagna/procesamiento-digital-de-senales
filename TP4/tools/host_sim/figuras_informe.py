"""Genera las figuras SIMULADAS del informe del TP4.

Corre p1_sim y p2_sim (el mismo código C del firmware, compilado en la PC con
la misma CMSIS-DSP) con los mismos experimentos que tp4_experimentos.m corre
en la placa, y grafica con matplotlib.

Uso (desde TP4/tools/host_sim, después de `make`):
    python3 figuras_informe.py ../../../documentacion/TP4/img
"""
import csv
import io
import os
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

AQUI = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, "img")
os.makedirs(OUT, exist_ok=True)
REG = os.path.join(OUT, "..", "registros_sim")
os.makedirs(REG, exist_ok=True)

plt.rcParams.update({"figure.figsize": (7.0, 3.8), "axes.grid": True, "grid.alpha": 0.35,
                     "font.size": 10, "legend.fontsize": 8.5, "savefig.dpi": 200,
                     "savefig.bbox": "tight", "figure.constrained_layout.use": True,
                     "axes.formatter.use_locale": False})

MU_LIST = [0.002, 0.01, 0.05, 0.1, 0.19, 0.21]
ALG = {"clase": 0, "cmsis": 1, "nlms": 2}


# --------------------------------------------------------------------------
# Corre los simuladores y decodifica el texto (mismo formato que la placa)
# --------------------------------------------------------------------------
def correr(binario, *args, nombre=None):
    txt = subprocess.run([os.path.join(AQUI, binario), *map(str, args)], capture_output=True,
                         text=True).stdout
    if nombre:
        with open(os.path.join(REG, nombre), "w") as f:
            f.write(txt)
    return txt


def kv(resto):
    d = {}
    for p in resto.split(","):
        if "=" in p:
            k, v = p.split("=", 1)
            try:
                d[k] = float(v)
            except ValueError:
                d[k] = v
    return d


def parse(txt):
    p1, p2, cur1, cur2 = [], [], None, None
    for ln in txt.splitlines():
        tag, _, resto = ln.strip().partition(",")
        if tag == "P1I":
            cur1 = {"cfg": kv(resto), "n": [], "mse": [], "werr2": [], "nd": [], "diff": []}
        elif tag == "P1P":
            cur1["planta"] = np.array(resto.split(","), float)
        elif tag == "P1M":
            v = [float(x) for x in resto.split(",")]
            cur1["n"].append(v[0]); cur1["mse"].append(v[1]); cur1["werr2"].append(v[2])
        elif tag == "P1D":
            v = np.array(resto.split(","), float)
            cur1["nd"].append(v[0]); cur1["diff"].append(v[1:])
        elif tag == "P1W":
            cur1["w"] = np.array(resto.split(","), float)
        elif tag == "P1F":
            p = resto.split(",")
            cur1["fin"] = {"n": int(p[0]), "mse": float(p[1]), "werr2": float(p[2]), "estado": p[4]}
            for k in ("n", "mse", "werr2", "nd"):
                cur1[k] = np.array(cur1[k], float)
            cur1["diff"] = np.array(cur1["diff"])
            p1.append(cur1)
        elif tag == "P2I":
            cur2 = {"cfg": kv(resto), "rows": []}
            p2.append(cur2)
        elif tag == "P2M":
            cur2["rows"].append([float(x) for x in resto.split(",")])
        elif tag == "P2W":
            cur2["w"] = np.array(resto.split(","), float)
        elif tag == "P2O":
            cur2["w_opt"] = np.array(resto.split(","), float)
    for c in p2:
        r = np.array(c["rows"])
        c["n"], c["p_ruido"], c["p_res"], c["red_dB"], c["werr2"] = r[:, 0], r[:, 1], r[:, 2], r[:, 3], r[:, 4]
        c["snr_in"], c["snr_out"] = r[:, 5], r[:, 6]
        c["t"] = c["n"] / c["cfg"]["fs"]
    return p1, p2


def p1(alg="cmsis", mu=0.01, entrada=0, A=1.0, sv=0.0, N=20000, seed=1, diff=1, nombre=None):
    return parse(correr("p1_sim", ALG[alg], mu, entrada, A, sv, N, seed, diff, nombre=nombre))[0][0]


def db(x):
    return 10 * np.log10(np.maximum(x, 1e-30))


def g(x):
    """Número con coma decimal para leyendas."""
    return f"{x:g}".replace(".", ",")


def etiqueta(r):
    s = rf"$\mu$ = {g(r['cfg']['mu'])}"
    return s + " (diverge)" if r["fin"]["estado"] == "DIVERGE" else s


def guardar(fig, nombre):
    fig.savefig(os.path.join(OUT, nombre + ".pdf"))
    fig.savefig(os.path.join(OUT, nombre + ".png"))
    plt.close(fig)
    print("  ", nombre)


def curvas(runs, campo, titulo, nombre, ylabel, xmax=None, ylim=None):
    fig, ax = plt.subplots()
    for r in runs:
        ax.plot(r["n"], db(r[campo]), lw=1.2, label=etiqueta(r))
    ax.set_xlabel("muestra n")
    ax.set_ylabel(ylabel)
    ax.set_title(titulo)
    if xmax:
        ax.set_xlim(0, xmax)
    ax.set_ylim(*(ylim or (-150, 40)))
    ax.legend(loc="upper right", ncol=2)
    guardar(fig, nombre)


# --------------------------------------------------------------------------
print("Parte 1")
resumen = []

# E1: entrada aleatoria, barrido de mu
e1 = [p1(mu=m, nombre=f"p1_e1_mu{m:g}.txt") for m in MU_LIST]
curvas(e1, "mse", r"Curva de aprendizaje, entrada aleatoria $A$ = 1 ($P_x$ = 1/3, $\mu_{max}$ = 0,2)",
       "p1_e1_mse", "MSE promedio de 100 muestras (dB)")
curvas(e1, "werr2", "Error de los coeficientes, entrada aleatoria", "p1_e1_werr",
       r"$\|\mathbf{p}-\mathbf{w}\|^2$ (dB)")
curvas(e1[:3], "mse", r"Detalle de las primeras 3000 muestras", "p1_e1_mse_zoom",
       "MSE (dB)", xmax=3000, ylim=(-80, 5))
for r in e1:
    resumen.append(("RANDOM", r))

# Diferencia de coeficientes en el tiempo (mu = 0,01)
r = e1[1]
fig, (a1, a2) = plt.subplots(2, 1, figsize=(7, 5.2), sharex=True)
nd = np.r_[0, r["nd"]]
dif = np.vstack([r["planta"], r["diff"]])
for k in range(6):
    a1.plot(nd, dif[:, k], lw=1.1, label=f"k = {k}")
a1.set_ylabel(r"$p_k - w_k$")
a1.set_title(r"Diferencia planta $-$ filtro cada 100 muestras ($\mu$ = 0,01, entrada aleatoria)")
a1.legend(ncol=3, loc="upper right")
im = a2.imshow(np.abs(dif.T), aspect="auto", origin="lower", extent=[0, nd[-1], -0.5, 29.5],
               cmap="viridis")
a2.set_ylabel("coeficiente k")
a2.set_xlabel("muestra n")
a2.set_xlim(0, 3000)
a1.set_xlim(0, 3000)
fig.colorbar(im, ax=a2, label=r"$|p_k - w_k|$")
guardar(fig, "p1_e1_diff")

# Coeficientes finales vs planta
fig, ax = plt.subplots()
k = np.arange(30)
ax.stem(k, r["planta"], linefmt="k-", markerfmt="ko", basefmt="k-", label="planta $p_k$")
ax.plot(k, e1[0]["w"], "s", mfc="none", label=r"$w_k$, $\mu$ = 0,002")
ax.plot(k, e1[1]["w"], "x", label=r"$w_k$, $\mu$ = 0,01")
ax.set_xlabel("k")
ax.set_ylabel("coeficiente")
ax.set_title("Coeficientes al final de 20000 muestras (entrada aleatoria)")
ax.legend()
guardar(fig, "p1_e1_coef")

# E2: entrada constante
e2 = [p1(mu=m, entrada=1, nombre=f"p1_e2_mu{m:g}.txt") for m in [0.002, 0.01, 0.05, 0.06, 0.07]]
curvas(e2, "mse", r"Curva de aprendizaje, entrada constante $A$ = 1 ($\mu_{max}$ = 0,0667)",
       "p1_e2_mse", "MSE (dB)")
curvas(e2, "werr2", "Error de los coeficientes, entrada constante", "p1_e2_werr",
       r"$\|\mathbf{p}-\mathbf{w}\|^2$ (dB)", ylim=(-10, 40))
fig, ax = plt.subplots()
ax.stem(k, e2[1]["planta"], linefmt="k-", markerfmt="ko", basefmt="k-", label="planta $p_k$")
for rr, mk in zip(e2[:3], ["s", "x", "^"]):
    ax.plot(k, rr["w"], mk, mfc="none", label=rf"$w_k$, $\mu$ = {g(rr['cfg']['mu'])}")
ax.set_xlabel("k")
ax.set_ylabel("coeficiente")
ax.set_title(r"Entrada constante: el MSE llega a 0 pero $\mathbf{w} \neq \mathbf{p}$ "
             rf"($\sum p_k$ = {g(round(e2[1]['planta'].sum(), 3))}, $\sum w_k$ = {g(round(e2[1]['w'].sum(), 3))})")
ax.legend()
guardar(fig, "p1_e2_coef")
for r in e2:
    resumen.append(("CONST", r))

# E3: potencia de entrada, LMS vs NLMS
e3 = [p1(mu=0.01, A=A, diff=0, nombre=f"p1_e3_lms_A{A:g}.txt") for A in [0.5, 1, 2, 5]]
e3n = [p1(alg="nlms", mu=0.3, A=A, diff=0, nombre=f"p1_e3_nlms_A{A:g}.txt") for A in [0.5, 1, 2, 5]]
fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.6), sharey=True)
for r in e3:
    lab = f"A = {g(r['cfg']['A'])}" + (" (diverge)" if r["fin"]["estado"] == "DIVERGE" else "")
    a1.plot(r["n"], db(r["mse"]), lw=1.2, label=lab)
for r in e3n:
    a2.plot(r["n"], db(r["mse"]), lw=1.2, label=f"A = {g(r['cfg']['A'])}")
a1.set_title(r"LMS, $\mu$ = 0,01")
a2.set_title(r"NLMS, $\mu$ = 0,3")
for a in (a1, a2):
    a.set_xlabel("muestra n")
    a.set_xlim(0, 6000)
    a.set_ylim(-150, 40)
    a.legend(loc="upper right")
a1.set_ylabel("MSE (dB)")
guardar(fig, "p1_e3_potencia")
for r in e3 + e3n:
    resumen.append(("POT", r))

# E4: implementaciones
e4 = [p1(alg=a, mu=(0.3 if a == "nlms" else 0.01), diff=0) for a in ("clase", "cmsis", "nlms")]
fig, ax = plt.subplots()
for r, ls in zip(e4, ["-", "--", "-"]):
    ax.plot(r["n"], db(r["mse"]), ls, lw=1.3,
            label=f"{r['cfg']['alg']}, " + rf"$\mu$ = {g(r['cfg']['mu'])}")
ax.set_xlabel("muestra n")
ax.set_ylabel("MSE (dB)")
ax.set_xlim(0, 6000)
ax.set_ylim(-150, 10)
ax.set_title("Las tres implementaciones (misma entrada)")
ax.legend()
guardar(fig, "p1_e4_alg")
iguales = np.array_equal(e4[0]["mse"], e4[1]["mse"])
print("   LMS de clase == arm_lms_f32:", iguales)

# E5: ruido de medición -> desajuste
SV = 0.01
e5 = [p1(mu=m, sv=SV, diff=0, nombre=f"p1_e5_mu{m:g}.txt") for m in [0.002, 0.01, 0.05, 0.1]]
curvas(e5, "mse", r"Con ruido de medición $\sigma_v$ = 0,01 ($\sigma_v^2$ = $-40$ dB)", "p1_e5_mse",
       "MSE (dB)", ylim=(-50, 5))
tabla5 = []
for r in e5:
    mu = r["cfg"]["mu"]
    trR = 30 * r["cfg"]["Px"]
    M = (mu / 2 * trR) / (1 - mu / 2 * trR)
    mse_est = r["mse"][-50:].mean()
    tabla5.append((mu, M, SV**2 * (1 + M), mse_est, mse_est / SV**2 - 1))

# --------------------------------------------------------------------------
print("Parte 2")


def p2(mu=0.005, topo=0, ruido=0.4, alg="cmsis", segs=6.0, ondas=None, dec=1, nombre=None):
    args = [ALG[alg], mu, ruido, topo, 8000, 200, 0.5, segs]
    if ondas:
        args += [ondas, dec]
    return parse(correr("p2_sim", *args, nombre=nombre))[1][0]


e6 = [p2(mu=m, nombre=f"p2_e6_mu{m:g}.txt") for m in [0.001, 0.005, 0.02, 0.1]]
fig, ax = plt.subplots()
def suave(r, nb=10):
    v = np.ones(nb) / nb
    return 10 * np.log10(np.convolve(r["p_ruido"], v, "same") / np.convolve(r["p_res"], v, "same"))


for i, r in enumerate(e6):
    ax.plot(r["t"], r["red_dB"], lw=0.5, alpha=0.3, color=f"C{i}")
    ax.plot(r["t"], suave(r), lw=1.5, color=f"C{i}", label=rf"$\mu$ = {g(r['cfg']['mu'])}")
ax.set_xlabel("tiempo (s)")
ax.set_ylabel(r"reducción del ruido (dB)")
ax.set_title(r"Cancelador: $10\log_{10}(P_{ruido}/P_{residuo})$ (finas: cada 100 muestras; gruesas: cada 1000)")
ax.legend(loc="lower right")
guardar(fig, "p2_e6_red")
fig, ax = plt.subplots()
for r in e6:
    ax.plot(r["t"], db(r["werr2"]), lw=1.0, label=rf"$\mu$ = {g(r['cfg']['mu'])}")
ax.set_xlabel("tiempo (s)")
ax.set_ylabel(r"$\|\mathbf{w}_{opt}-\mathbf{w}\|^2$ (dB)")
ax.set_title(r"Convergencia al filtro óptimo $\mathbf{w}_{opt} = \mathbf{c}$ (correlador)")
ax.legend(loc="upper right")
guardar(fig, "p2_e6_werr")
r = e6[1]
fig, ax = plt.subplots()
ax.stem(k, r["w_opt"], linefmt="k-", markerfmt="ko", basefmt="k-", label=r"óptimo $c_k$")
ax.plot(k, r["w"], "rx", label=r"$w_k$ al final ($\mu$ = 0,005)")
ax.set_xlabel("k")
ax.set_ylabel("coeficiente")
ax.set_title("Coeficientes del cancelador vs. los del correlador")
ax.legend()
guardar(fig, "p2_e6_coef")

e7 = [p2(mu=m, topo=1, nombre=f"p2_e7_mu{m:g}.txt") for m in [0.005, 0.02]]
fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.6))
for r in e7:
    a1.plot(r["t"], suave(r), lw=1.2, label=rf"$\mu$ = {g(r['cfg']['mu'])}")
a1.plot(e6[1]["t"], suave(e6[1]), "k", lw=0.8, alpha=0.6, label=r"correlador en el ruido, $\mu$ = 0,005")
a1.set_xlabel("tiempo (s)")
a1.set_ylabel("reducción (dB)")
a1.set_title("Correlador en la referencia")
a1.legend(loc="lower right", fontsize=7.5)
a2.stem(k, e7[1]["w_opt"], linefmt="k-", markerfmt="ko", basefmt="k-", label=r"$\approx C^{-1}$")
a2.plot(k, e7[1]["w"], "rx", label=r"$w_k$ ($\mu$ = 0,02)")
a2.set_xlabel("k")
a2.set_title("Coeficientes")
a2.legend()
guardar(fig, "p2_e7_ref")

# Formas de onda ("osciloscopio simulado")
f_ondas = os.path.join(REG, "p2_ondas.csv")
p2(mu=0.005, segs=4.0, ondas=f_ondas)
with open(f_ondas) as f:
    W = np.array([[float(x) for x in row] for row in list(csv.reader(f))[1:]])
fs = 8000.0
fig, axs = plt.subplots(2, 1, figsize=(7, 5), sharey=True)
for a, t0, tit in ((axs[0], 0.0, "Al arrancar (coeficientes en cero)"),
                   (axs[1], 3.5, "Después de converger (t = 3,5 s)")):
    i0 = int(t0 * fs)
    sl = slice(i0, i0 + int(0.02 * fs))
    tt = (W[sl, 0] - i0) / fs * 1e3
    a.plot(tt, W[sl, 2], color="0.65", lw=1.0, label="d = s + ruido (entrada)")
    a.plot(tt, W[sl, 3], "C3", lw=1.3, label="e (salida del cancelador)")
    a.plot(tt, W[sl, 1], "k--", lw=1.0, label="s (señal del generador)")
    a.set_title(tit)
    a.set_ylabel("V")
axs[1].set_xlabel("tiempo (ms)")
axs[0].legend(loc="upper right", ncol=3, fontsize=7.5)
guardar(fig, "p2_ondas")

# --------------------------------------------------------------------------
# Tablas para el informe (LaTeX), con coma decimal
import re  # noqa: E402


def coma(linea):
    return re.sub(r"(\d)\.(\d)", r"\1{,}\2", linea)


class Coma:
    def __init__(self, f):
        self.f = f

    def write(self, s):
        self.f.write(coma(s))


with open(os.path.join(OUT, "..", "src", "tablas_sim.tex"), "w") as f_real:
    f = Coma(f_real)
    f.write("% Generado por TP4/tools/host_sim/figuras_informe.py\n")
    f.write("\\newcommand{\\TablaPunoSim}{%\n\\begin{tabular}{llrrrrrl}\n\\toprule\n")
    f.write("Entrada & Alg. & $A$ & $\\mu$ & $\\mu_{max}$ & $\\tau$ teó. & MSE final & Estado \\\\\n\\midrule\n")
    for ent, r in resumen:
        c = r["cfg"]
        f.write(f"{ent.lower()} & {c['alg'].replace('_', chr(92) + '_')} & {c['A']:g} & {c['mu']:g} & "
                f"{c['mu_max']:.3g} & {c['tau']:.3g} & {r['fin']['mse']:.2e} & {r['fin']['estado'].lower()} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}}\n\n")
    f.write("\\newcommand{\\TablaDesajusteSim}{%\n\\begin{tabular}{rrrrr}\n\\toprule\n")
    f.write("$\\mu$ & $M$ teórico & MSE teórico & MSE medido & $M$ medido \\\\\n\\midrule\n")
    for mu, M, teo, med, Mm in tabla5:
        f.write(f"{mu:g} & {M:.3f} & {teo:.3e} & {med:.3e} & {Mm:.3f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}}\n\n")
    f.write("\\newcommand{\\TablaCanceladorSim}{%\n\\begin{tabular}{lrrrr}\n\\toprule\n")
    f.write("Topología & $\\mu$ & $\\tau$ teó. (s) & Reducción (dB) & SNR salida (dB) \\\\\n\\midrule\n")
    for r in e6 + e7:
        c = r["cfg"]
        f.write(f"{'ruido' if c['topo'] == 'RUIDO' else 'referencia'} & {c['mu']:g} & "
                f"{c['tau'] / c['fs']:.3f} & {r['red_dB'][-20:].mean():.1f} & {r['snr_out'][-20:].mean():.1f} \\\\\n")
    f.write("\\bottomrule\n\\end{tabular}}\n")
print("Listo:", OUT)
