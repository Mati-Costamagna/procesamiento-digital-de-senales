%TP4_EXPERIMENTOS Corre en la placa todos los experimentos del informe.
%
%   1. Conectar la placa (USB del MCU-Link, J17) y cargarle el firmware.
%   2. Cerrar cualquier terminal que tenga abierto el puerto (MCUXpresso, PuTTY).
%   3. Poner abajo el puerto correcto y ejecutar este script (F5).
%      Para la Parte 2 tiene que estar conectado el generador (ver README).
%
%   Guarda los registros en ./registros y las figuras en ./img.
%   Se puede correr por secciones (Ctrl+Enter en cada %%).

PUERTO = 'COM3';
REG = 'registros';
IMG = 'img';
if ~exist(REG, 'dir'), mkdir(REG); end
if ~exist(IMG, 'dir'), mkdir(IMG); end
r = @(nombre) fullfile(REG, nombre);

%% Parte 1 - E1: entrada aleatoria, A = 1, barrido de mu (LMS de CMSIS)
d = tp4_captura(PUERTO, {'1', 'G1', 'E0', 'A1', 'V0', 'R1', 'N20000', 'D1', 'B'}, ...
    'P1B,FIN', r('p1_e1_random_barrido.txt'));
tp4_graficos_p1(d, '(entrada aleatoria, A = 1)', IMG, 'p1_e1_random');

%% Parte 1 - E2: entrada constante, A = 1, barrido de mu
d = tp4_captura(PUERTO, {'1', 'G1', 'E1', 'A1', 'V0', 'R1', 'N20000', 'D1', 'B'}, ...
    'P1B,FIN', r('p1_e2_const_barrido.txt'));
tp4_graficos_p1(d, '(entrada constante, A = 1)', IMG, 'p1_e2_const');

%% Parte 1 - E3: influencia de la potencia de entrada (mu fijo = 0,01)
corridas = [];
for A = [0.5 1 2 3]
    d = tp4_captura(PUERTO, {'1', 'G1', 'E0', sprintf('A%g', A), 'U0.01', 'V0', 'R1', 'D0', 'S'}, ...
        'P1F', r(sprintf('p1_e3_lms_A%g.txt', A)));
    corridas = [corridas; d.p1]; %#ok<AGROW>
end
d3.p1 = corridas;
tp4_graficos_p1(d3, '(LMS, \mu = 0,01, distintas A)', IMG, 'p1_e3_potencia_lms');
% El mismo experimento con NLMS: el paso se normaliza con la energía de la entrada
corridas = [];
for A = [0.5 1 2 3]
    d = tp4_captura(PUERTO, {'1', 'G2', 'E0', sprintf('A%g', A), 'U0.3', 'V0', 'R1', 'D0', 'S'}, ...
        'P1F', r(sprintf('p1_e3_nlms_A%g.txt', A)));
    corridas = [corridas; d.p1]; %#ok<AGROW>
end
d3n.p1 = corridas;
tp4_graficos_p1(d3n, '(NLMS, \mu = 0,3, distintas A)', IMG, 'p1_e3_potencia_nlms');

%% Parte 1 - E4: comparación de implementaciones (mismo experimento)
corridas = [];
for G = 0:2
    mu = 0.01;
    if G == 2, mu = 0.3; end
    d = tp4_captura(PUERTO, {'1', sprintf('G%d', G), 'E0', 'A1', sprintf('U%g', mu), 'V0', ...
        'R1', 'D0', 'S'}, 'P1F', r(sprintf('p1_e4_alg%d.txt', G)));
    corridas = [corridas; d.p1]; %#ok<AGROW>
end
d4.p1 = corridas;
tp4_graficos_p1(d4, '(LMS de clase / arm\_lms\_f32 / NLMS)', IMG, 'p1_e4_algoritmos');
fprintf('Ciclos por muestra: clase %g, arm_lms_f32 %g, arm_lms_norm_f32 %g\n', ...
    d4.p1(1).fin.cyc, d4.p1(2).fin.cyc, d4.p1(3).fin.cyc);

%% Parte 1 - E5: ruido de medición (sigma_v = 0,01): desajuste vs. mu
d = tp4_captura(PUERTO, {'1', 'G1', 'E0', 'A1', 'V0.01', 'R1', 'D0', 'B'}, ...
    'P1B,FIN', r('p1_e5_ruido_barrido.txt'));
tp4_graficos_p1(d, '(\sigma_v = 0,01)', IMG, 'p1_e5_ruido');

%% Parte 2 - E6: cancelador, distintos mu (correlador en el camino del ruido)
% Generador: senoidal 200 Hz, 1 Vpp, offset 1,65 V (ver README)
for mu = [0.001 0.005 0.02 0.1]
    tp4_captura(PUERTO, {'2', 'Q0', 'G1', 'A0.4', 'O1', sprintf('U%g', mu), 'Z'}, 8, ...
        r(sprintf('p2_e6_mu%g.txt', mu)), struct('vivo', false));
    tp4_captura(PUERTO, {'C', 'T'}, 1, r(sprintf('p2_e6_mu%g_coef.txt', mu)), struct('vivo', false));
end
% Junta las corridas (cada una desde su reinicio 'Z') y grafica
tramos = [];
for mu = [0.001 0.005 0.02 0.1]
    a = tp4_parse_log(r(sprintf('p2_e6_mu%g.txt', mu)));
    b = tp4_parse_log(r(sprintf('p2_e6_mu%g_coef.txt', mu)));
    t = a.p2(end);
    if ~isempty(b.p2)
        t.w = b.p2(end).w; t.w_opt = b.p2(end).w_opt; t.tiempos = b.p2(end).tiempos;
    end
    tramos = [tramos; t]; %#ok<AGROW>
end
d6.p2 = tramos;
tp4_graficos_p2(d6, '', IMG, 'p2_e6_mu');

%% Parte 2 - E7: correlador en la rama de la referencia (como la figura 2)
for mu = [0.005 0.02]
    tp4_captura(PUERTO, {'2', 'Q1', 'G1', 'A0.4', 'O1', sprintf('U%g', mu), 'Z'}, 8, ...
        r(sprintf('p2_e7_ref_mu%g.txt', mu)), struct('vivo', false));
end
tramos = [];
for mu = [0.005 0.02]
    a = tp4_parse_log(r(sprintf('p2_e7_ref_mu%g.txt', mu)));
    tramos = [tramos; a.p2(end)]; %#ok<AGROW>
end
d7.p2 = tramos;
tp4_graficos_p2(d7, '(correlador en la referencia)', IMG, 'p2_e7_ref');

% Vuelve a la configuración por defecto
tp4_captura(PUERTO, {'Q0', 'U0.005', 'Z'}, 1, '', struct('vivo', false));
