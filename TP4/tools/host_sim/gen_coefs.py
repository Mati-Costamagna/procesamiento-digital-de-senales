"""Genera los coeficientes fijos de coefs.c (planta y correlador), L = 8."""
import numpy as np

L = 8
k = np.arange(L)
planta = np.round(0.8 * np.exp(-k / 3) * np.sin(0.3 * np.pi * (k + 1)) + 0.05 * np.cos(0.9 * k), 4)
a = 0.7
correlador = np.round(np.sqrt(1 - a * a) * a ** k, 4)

for nombre, h in (("g_planta", planta), ("g_correlador", correlador)):
    print(f"{nombre}: energia = {np.sum(h**2):.4f}, suma = {np.sum(h):.4f}")
    print(", ".join(f"{v:.4f}f" for v in h))
