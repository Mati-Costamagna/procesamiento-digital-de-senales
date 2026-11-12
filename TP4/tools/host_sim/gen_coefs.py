"""Genera los coeficientes fijos de coefs.c (planta y correlador)."""
import numpy as np

k = np.arange(30)
planta = np.round(0.8 * np.exp(-k / 8) * np.sin(0.3 * np.pi * (k + 1)) + 0.05 * np.cos(0.9 * k), 4)
correlador = np.round(0.6 * 0.8 ** k, 4)

for nombre, h in (("g_planta", planta), ("g_correlador", correlador)):
    print(f"{nombre}: energia = {np.sum(h**2):.4f}")
    print(", ".join(f"{v:.4f}f" for v in h))
