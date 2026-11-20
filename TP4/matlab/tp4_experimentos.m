%TP4_EXPERIMENTOS Corre en la placa todos los experimentos del informe.
%
%   1. Conectar la placa (USB del MCU-Link, J17) y cargarle el firmware
%      (proyecto TP4_core0, que lleva adentro la imagen del TP4_core1).
%   2. Cerrar cualquier terminal que tenga abierto el puerto (MCUXpresso, PuTTY).
%   3. Poner abajo el puerto correcto y ejecutar este script (F5).
%      Para la Parte 2 tiene que estar conectado el generador (ver README).
%
%   El filtro adaptativo es arm_lms_q15 de CMSIS-DSP (punto fijo Q15).
%   El comando U fija el mu de arm_lms_q15 (fracción Q15, 0 < mu < 1).
%
%   La comunicación la atiende el core1 de la placa: mientras manda los datos
%   a MATLAB, el core0 sigue procesando en tiempo real.
%
%   Guarda los registros en ./registros y las figuras en ./img.
%   Se puede correr por secciones (Ctrl+Enter en cada %%).

PUERTO = 'COM5';
REG = 'registros';
IMG = 'img';
if ~exist(REG, 'dir'), mkdir(REG); end
if ~exist(IMG, 'dir'), mkdir(IMG); end
r = @(nombre) fullfile(REG, nombre);
sin_vivo = struct('vivo', false);

%% Parte 1 - E1: entrada aleatoria, A = 1, barrido de mu
% Barrido: mu = 0,002 / 0,01 / 0,05 / 0,2 / 0,5 / 0,7 (límite teórico 0,6 con L = 10).
% En Q15 el MSE no baja indefinidamente: queda un piso de cuantización que
% es más bajo cuanto más grande es mu.
d = tp4_captura(PUERTO, {'1', 'E0', 'A1', 'V0', 'R1', 'N5000', 'D1', 'B'}, ...
    'P1B,FIN', r('p1_e1_random_barrido.txt'));
tp4_graficos_p1(d, '(entrada aleatoria, A = 1)', IMG, 'p1_e1_random');

%% Parte 1 - E2: entrada constante, A = 1, barrido de mu (límite 2/(L A^2) = 0,2)
d = tp4_captura(PUERTO, {'1', 'E1', 'A1', 'V0', 'R1', 'N5000', 'D1', 'B'}, ...
    'P1B,FIN', r('p1_e2_const_barrido.txt'));
tp4_graficos_p1(d, '(entrada constante, A = 1)', IMG, 'p1_e2_const');

%% Parte 1 - E3: influencia de la potencia de entrada (mu fijo = 0,05)
% En Q15 la amplitud tiene que cumplir A <= 1.
corridas = [];
for A = [0.125 0.25 0.5 1]
    d = tp4_captura(PUERTO, {'1', 'E0', sprintf('A%g', A), 'U0.05', 'V0', 'R1', 'N5000', 'D0', 'S'}, ...
        'P1F', r(sprintf('p1_e3_A%g.txt', A)));
    corridas = [corridas; d.p1]; %#ok<AGROW>
end
d3.p1 = corridas;
tp4_graficos_p1(d3, '(\mu = 0,05, distintas A)', IMG, 'p1_e3_potencia');

%% Parte 1 - E4: efecto de Q15 con poca potencia de entrada (A = 0,25)
% Si |e mu x| es menor que 1 LSB, arm_lms_q15 trunca la actualización a 0 (o
% a -1 LSB) y el filtro deja de aprender ("stalling"): con mu chico casi no
% identifica la planta, aunque en punto flotante convergería.
corridas = [];
for mu = [0.002 0.01 0.05 0.2 0.5]
    d = tp4_captura(PUERTO, {'1', 'E0', 'A0.25', sprintf('U%g', mu), 'V0', 'R1', 'N5000', 'D0', 'S'}, ...
        'P1F', r(sprintf('p1_e4_q15_mu%g.txt', mu)));
    corridas = [corridas; d.p1]; %#ok<AGROW>
end
d4.p1 = corridas;
tp4_graficos_p1(d4, '(A = 0,25: piso de cuantización de Q15)', IMG, 'p1_e4_q15');
fprintf('Ciclos por muestra de arm_lms_q15 (L = 10): %g\n', d4.p1(end).fin.cyc);

%% Parte 1 - E5: ruido de medición (sigma_v = 0,01): desajuste vs. mu
d = tp4_captura(PUERTO, {'1', 'E0', 'A1', 'V0.01', 'R1', 'N5000', 'D0', 'B'}, ...
    'P1B,FIN', r('p1_e5_ruido_barrido.txt'));
tp4_graficos_p1(d, '(\sigma_v = 0,01)', IMG, 'p1_e5_ruido');

%% Parte 2 - E6: cancelador con la topología de la figura 2 (Q1), distintos mu
% Generador: senoidal 200 Hz, 1 Vpp, offset 1,65 V en ADC0_A0 (J4[2]). Ver README.
% DAC0 (J1[4]) = e (salida), DAC1 (J1[2]) = d (señal + ruido AWGN, sigma = 0,25 V).
% postShift = 1 -> mu_ef = 2 mu. Con mu chico manda el piso de Q15; con mu
% grande, el desajuste que provoca la propia senoidal en el error.
mus = [0.005 0.01 0.02 0.05 0.1];
for mu = mus
    tp4_captura(PUERTO, {'2', 'M1', 'Q1', 'A0.25', 'O1', 'P0', sprintf('U%g', mu), 'Z'}, 8, ...
        r(sprintf('p2_e6_mu%g.txt', mu)), sin_vivo);
    tp4_captura(PUERTO, {'C', 'T'}, 1, r(sprintf('p2_e6_mu%g_coef.txt', mu)), sin_vivo);
end
% Junta las corridas (cada una desde su reinicio 'Z') y grafica
tramos = [];
for mu = mus
    a = tp4_parse_log(r(sprintf('p2_e6_mu%g.txt', mu)));
    b = tp4_parse_log(r(sprintf('p2_e6_mu%g_coef.txt', mu)));
    t = a.p2(end);
    if ~isempty(b.p2)
        t.w = b.p2(end).w; t.w_opt = b.p2(end).w_opt; t.c = b.p2(end).c; t.tiempos = b.p2(end).tiempos;
    end
    tramos = [tramos; t]; %#ok<AGROW>
end
d6.p2 = tramos;
tp4_graficos_p2(d6, '(figura 2: W_{opt} = C^{-1})', IMG, 'p2_e6_mu');

%% Parte 2 - E7: cancelador clásico (correlador en el camino del ruido, Q0)
mus7 = [0.01 0.02 0.05];
for mu = mus7
    tp4_captura(PUERTO, {'2', 'M1', 'Q0', 'A0.25', 'O1', 'P0', sprintf('U%g', mu), 'Z'}, 8, ...
        r(sprintf('p2_e7_ruido_mu%g.txt', mu)), sin_vivo);
    tp4_captura(PUERTO, {'C'}, 1, r(sprintf('p2_e7_ruido_mu%g_coef.txt', mu)), sin_vivo);
end
tramos = [];
for mu = mus7
    a = tp4_parse_log(r(sprintf('p2_e7_ruido_mu%g.txt', mu)));
    b = tp4_parse_log(r(sprintf('p2_e7_ruido_mu%g_coef.txt', mu)));
    t = a.p2(end);
    if ~isempty(b.p2)
        t.w = b.p2(end).w; t.w_opt = b.p2(end).w_opt; t.c = b.p2(end).c;
    end
    tramos = [tramos; t]; %#ok<AGROW>
end
d7.p2 = tramos;
tp4_graficos_p2(d7, '(correlador en el ruido: W_{opt} = C)', IMG, 'p2_e7_ruido');

%% Parte 2 - E8: formas de onda del aprendizaje (captura de 2000 muestras desde el reinicio)
% Z2000 pone los coeficientes en cero y graba d, e, y, s, x durante 2000
% muestras (0,25 s a 8 kHz). La transmisión tarda ~10 s a 115200 baudios;
% mientras tanto el core0 sigue cancelando el ruido en tiempo real.
d = tp4_captura(PUERTO, {'2', 'M0', 'Q1', 'A0.25', 'U0.05', 'Z2000'}, 'CAPF', ...
    r('p2_e8_captura.txt'), sin_vivo);
tp4_graficos_cap(d, '(desde el reinicio)', IMG, 'p2_e8');
% Una captura ya convergida (sin reiniciar): espectros de d y e
% d = tp4_captura(PUERTO, {'K2000'}, 'CAPF', r('p2_e8_captura_conv.txt'), sin_vivo);
tp4_graficos_cap(d, '(convergido)', IMG, 'p2_e8_conv');

% Vuelve a la configuración por defecto
tp4_captura(PUERTO, {'Q1', 'A0.25', 'U0.02', 'M1', 'Z'}, 1, '', sin_vivo);
