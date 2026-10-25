function disenar_multitasa(graficar)
% DISENAR_MULTITASA  TP2 - Filtros FIR en version multitasa, en Q15.
%
% Por que multitasa:
%   Con las bandas fijas en Hz, el orden de un FIR crece como fs / df. El PA
%   y el flanco inferior del PBanda (transicion 20..50 Hz) y el EB
%   (transiciones de 7.5 Hz) piden miles de coeficientes a 44 y 48 kHz (ver
%   resumen_filtros.csv de disenar_PB_PA.m). Pero todo lo angosto esta por
%   debajo de 65 Hz: no hace falta resolverlo a 48 kHz.
%
% Estructura (una sola tasa baja F_BAJA = 1 kHz para las cinco fs, porque
% 8k, 16k, 22k, 44k y 48k son multiplos enteros de 1 kHz):
%
%   x --[HD]--(M:1)--[G]--(1:M)--[M*HD]--> v      camino de banda angosta
%
%   PB     : FIR directo a fs (su transicion es de 200 Hz, ya es corto)
%   PA     : y = x[n-D] - v      G = G_PA, pasa bajos  20..50 Hz
%   EB     : y = x[n-D] - v      G = G_EB, pasa banda 42.5..57.5 Hz
%   PBanda : y = LPB( PA(x) )    LPB = pasa bajos directo 3400..3600 Hz
%
%   - El mismo HD sirve de diezmador y de interpolador (con ganancia M):
%     los dos tienen que dejar pasar 0..65 Hz y rechazar lo que al cambiar
%     de tasa cae encima de esa banda (desde F_BAJA - 65 Hz).
%   - G_PA y G_EB se disenan UNA vez a F_BAJA y sirven para las cinco fs.
%   - PA y EB salen por complemento: x[n-D] menos el camino angosto. Para
%     que funcione, todos los filtros del camino son de fase lineal y de
%     longitud impar, y D = (NHD - 1) + M*(NG - 1)/2 es un entero exacto.
%   - PBanda va en cascada: el rizado de 1 dB se reparte entre PA y LPB,
%     por eso G_PA y LPB se disenan con la mitad del presupuesto.
%
% Que verifica (todo con los coeficientes ya cuantizados a Q15):
%   1. Cada bloque: orden minimo que cumple su especificacion en Q15.
%   2. Cada filtro compuesto contra la especificacion ORIGINAL (rizado,
%      atenuacion y ganancia maxima), con su respuesta equivalente exacta.
%   3. Las componentes espurias del cambio de tasa (aliasing e imagenes):
%      el sistema no es LTI, asi que su nivel total se acota aparte.
%   4. En el tiempo: el modelo muestra a muestra (el que hay que pasar a C)
%      contra el modelo vectorizado, y tonos medidos contra la respuesta
%      calculada.
%
% Salidas: filtros_multitasa.h (para el firmware), resumen_multitasa.csv y
% filtros_multitasa.mat. NO toca FiltrosTP2.h.
%
% Uso: disenar_multitasa          (con graficos)
%      disenar_multitasa(false)   (sin graficos)
% Necesita Signal Processing Toolbox (firpm, firpmord). Los ordenes son de
% cientos de coeficientes, asi que firpm no tiene problemas numericos.

if nargin < 1, graficar = true; end

%% ======================= CONFIGURACION =======================
FS        = [8000 16000 22000 44000 48000];
FS_NOMBRE = {'8k', '16k', '22k', '44k', '48k'};
F_BAJA    = 1000;    % tasa baja comun [Hz]; fs / F_BAJA tiene que ser entero
APASS     = 1;       % rizado maximo pico a pico en la banda de paso [dB]

% Mismos bordes que disenar_PB_PA.m (la Fc pedida queda en el centro de
% cada transicion)
tipos(1) = struct('nombre', 'PB',     'f', [3500 3700],        'a', [1 0],   'Astop', 30);
tipos(2) = struct('nombre', 'PA',     'f', [20 50],            'a', [0 1],   'Astop', 30);
tipos(3) = struct('nombre', 'PBanda', 'f', [20 50 3400 3600],  'a', [0 1 0], 'Astop', 30);
tipos(4) = struct('nombre', 'EB',     'f', [35 42.5 57.5 65],  'a', [1 0 1], 'Astop', 25);

% Diezmador / interpolador HD
HD_RIZADO       = 0.002;  % desvio en la banda de paso (se suma al error del PA y del EB)
HD_ATEN         = 60;     % [dB] rechazo de lo que se superpone a la banda de paso
ESPURIAS_MAX_DB = -40;    % nivel total admitido de aliasing + imagenes [dB]
MARGEN          = 0.85;   % fraccion del presupuesto de error que usa cada bloque

ARCHIVO_H   = 'filtros_multitasa.h';
ARCHIVO_CSV = 'resumen_multitasa.csv';
ARCHIVO_MAT = 'filtros_multitasa.mat';
CSV_DIRECTO = 'resumen_filtros.csv';   % de disenar_PB_PA.m, para comparar

% Solo para las columnas informativas de uso de CPU
F_CPU          = 150e6;   % Cortex-M33 del MCXN947
CICLOS_POR_TAP = 5;       % el mismo estimado que disenar_PB_PA.m

%% ======================= ESPECIFICACION DE CADA BLOQUE =======================
dp  = (10^(APASS/20) - 1) / (10^(APASS/20) + 1);   % 1 dB pico a pico
dpc = (10^(APASS/40) - 1) / (10^(APASS/40) + 1);   % medio dB (PA y LPB en cascada)
ds  = @(A) 10^(-A/20);
M   = FS / F_BAJA;
if any(M ~= round(M))
  error('Todas las fs tienen que ser multiplos enteros de F_BAJA = %d Hz.', F_BAJA);
end
nFs = numel(FS);
nT  = numel(tipos);

% Lo mas alto que deja pasar el camino angosto (borde de paso del PA y del
% EB): HD tiene que ser plano hasta ahi y rechazar desde F_BAJA - F_PASO_HD,
% que es lo que al cambiar de tasa se superpone con 0..F_PASO_HD.
F_PASO_HD = max([tipos(2).f(end), tipos(4).f(end)]);
if F_PASO_HD >= F_BAJA / 2
  error('F_BAJA = %d Hz es muy baja para bordes de hasta %g Hz.', F_BAJA, F_PASO_HD);
end

% Lo que entra por la banda de transicion de HD (F_PASO_HD..F_BAJA-F_PASO_HD)
% se superpone, al diezmar, con la banda de rechazo de G, y el interpolador
% lo vuelve a sacar en F_BAJA - f. Su nivel es ~ dG_rechazo * HD(F_BAJA/2)^2
% ~ dG_rechazo / 4, asi que el rechazo de G tambien lo fija ESPURIAS_MAX_DB.
dGesp = 4 * 10^(ESPURIAS_MAX_DB/20);

% G_PA: su banda de paso (0..20 Hz) es la de rechazo del PA y su banda de
% rechazo (50 Hz..) es la de paso del PA, porque PA = 1 - camino angosto:
%   rechazo del PA: |1 - HD*G*HD| <= dG + 2*HD_RIZADO <= ds(30)
%   paso del PA   : |HD*G*HD| <= dG <= dpc (medio rizado, por la cascada)
bG(1) = struct('nombre', 'G_PA', 'f', tipos(2).f, 'a', [1 0], ...
               'dev', MARGEN * [ds(tipos(2).Astop) - 2*HD_RIZADO, min(dpc, dGesp)]);
% G_EB: lo mismo con EB = 1 - pasa banda
bG(2) = struct('nombre', 'G_EB', 'f', tipos(4).f, 'a', [0 1 0], ...
               'dev', MARGEN * [min(dp, dGesp), ds(tipos(4).Astop) - 2*HD_RIZADO, min(dp, dGesp)]);
% LPB: flanco superior del PBanda, con el rizado que deja libre el PA
da = bG(1).dev(2) * (1 + HD_RIZADO)^2;             % rizado maximo del PA
rr = 10^(APASS/20) * (1 - da) / (1 + da);
bLPB = struct('nombre', 'LPB', 'f', tipos(3).f(3:4), 'a', [1 0], ...
              'dev', MARGEN * [(rr - 1) / (rr + 1), ds(tipos(3).Astop) / (1 + da)]);
bPB  = struct('nombre', 'PB', 'f', tipos(1).f, 'a', tipos(1).a, ...
              'dev', [dp, ds(tipos(1).Astop)]);
bHD  = struct('nombre', 'HD', 'f', [F_PASO_HD, F_BAJA - F_PASO_HD], 'a', [1 0], ...
              'dev', [HD_RIZADO, ds(HD_ATEN)]);

%% ======================= DISENO DE LOS BLOQUES =======================
tTotal = tic;
fprintf('\n=== Bloques: orden minimo que cumple en Q15 ===\n');
fprintf('%-6s %6s %12s %8s %6s\n', 'Bloque', 'fs', 'N firpmord', 'N Q15', 'taps');
G(1) = orden_minimo(bG(1), F_BAJA);  imprimir_bloque(G(1));
G(2) = orden_minimo(bG(2), F_BAJA);  imprimir_bloque(G(2));
for i = 1:nFs
  HD(i)  = orden_minimo(bHD,  FS(i));  imprimir_bloque(HD(i));   %#ok<AGROW>
  PB(i)  = orden_minimo(bPB,  FS(i));  imprimir_bloque(PB(i));   %#ok<AGROW>
  LPB(i) = orden_minimo(bLPB, FS(i));  imprimir_bloque(LPB(i));  %#ok<AGROW>
end

%% ======================= VERIFICACION DE LOS FILTROS COMPUESTOS =======================
directo = leer_directo(CSV_DIRECTO);
res = struct([]);
fprintf('\n=== Filtros compuestos (coeficientes Q15) ===\n');
fprintf(['%-5s %-7s %8s %8s %8s %8s %9s %9s %8s %8s %9s %8s %6s\n'], 'fs', 'Filtro', ...
        'MAC pico', 'CPU pico', 'CPU rep.', 'CPU prom', 'taps dir.', 'CPU dir.', 'retardo', ...
        'rizado', 'atenuac.', 'espurias', 'cumple');
for i = 1:nFs
  fs  = FS(i);  Mi = M(i);
  hd  = HD(i).q / 32768;  Nd = numel(hd);
  Np  = Mi * ceil(Nd / Mi);                  % HD rellenado para el polifasico
  for k = 1:nT
    t = tipos(k);
    switch t.nombre
      case 'PB',     g = [];             post = PB(i).q / 32768;
      case 'PA',     g = G(1).q / 32768; post = [];
      case 'PBanda', g = G(1).q / 32768; post = LPB(i).q / 32768;
      case 'EB',     g = G(2).q / 32768; post = [];
    end
    R = analizar(t, fs, Mi, hd, g, post, dp, ESPURIAS_MAX_DB);

    % Costo por muestra de entrada: HD y G se calculan 1 de cada M muestras,
    % el interpolador polifasico usa Np/M coeficientes en cada muestra.
    % "rep": HD repartido, cada muestra que llega suma sus ceil(Nd/M)
    % productos a los acumuladores de los diezmados pendientes (mismo
    % resultado y mismo retardo, sin el pico de Nd productos de golpe).
    if isempty(g)
      pico = taps_cmsis(post);  prom = pico;  picoRep = pico;  Dc = NaN;
    else
      nG      = taps_cmsis(g);
      pico    = Nd + nG + Np / Mi;
      picoRep = ceil(Nd / Mi) + nG + Np / Mi;
      prom    = (Nd + nG) / Mi + Np / Mi;
      if ~isempty(post)
        pico    = pico + taps_cmsis(post);
        picoRep = picoRep + taps_cmsis(post);
        prom    = prom + taps_cmsis(post);
      end
      Dc = (Nd - 1) + Mi * (numel(g) - 1) / 2;     % retardo del complemento
    end

    % Prueba en el tiempo (PA y EB; el PBanda es el PA mas un FIR directo)
    eMod = NaN;  eTon = NaN;
    if any(strcmp(t.nombre, {'PA', 'EB'}))
      if strcmp(t.nombre, 'PA'), fTest = [10 35 100 1000]; else, fTest = [30 50 100 1000]; end
      [eMod, eTon] = probar_en_el_tiempo(hd, g, Mi, fs, R.hmain, fTest);
    end

    clave = sprintf('%d_%s', fs, t.nombre);
    if isKey(directo, clave), tDir = directo(clave); else, tDir = NaN; end

    R.fs = fs;  R.fsNombre = FS_NOMBRE{i};  R.tipo = t.nombre;  R.M = Mi;
    R.D = Dc;   R.pico = pico;  R.picoRep = picoRep;  R.prom = prom;
    R.cpuPico    = 100 * pico * CICLOS_POR_TAP * fs / F_CPU;
    R.cpuPicoRep = 100 * picoRep * CICLOS_POR_TAP * fs / F_CPU;
    R.cpuProm    = 100 * prom * CICLOS_POR_TAP * fs / F_CPU;
    R.tapsDir = tDir;
    R.cpuDir  = 100 * tDir * CICLOS_POR_TAP * fs / F_CPU;
    R.errModelo = eMod;  R.errTonos = eTon;
    R = rmfield(R, 'hmain');
    if isempty(res), res = R; else, res(end + 1) = R; end   %#ok<AGROW>

    if R.ok, ok = 'si'; else, ok = 'NO'; end
    fprintf('%-5s %-7s %8.0f %7.0f%% %7.0f%% %7.1f%% %9s %8s %6.1fms %6.2fdB %7.2fdB %6.1fdB %6s\n', ...
            FS_NOMBRE{i}, t.nombre, pico, R.cpuPico, R.cpuPicoRep, R.cpuProm, num2str(tDir), ...
            sprintf('%.0f%%', R.cpuDir), 1000 * R.retardo / fs, R.rip, R.att, R.espDB, ok);
  end
end
fprintf('CPU con %d ciclos por coeficiente a %.0f MHz. pico: muestra en la que se\n', ...
        CICLOS_POR_TAP, F_CPU / 1e6);
fprintf('diezma; rep.: pico con HD repartido entre las M muestras; prom: promedio.\n');
fprintf('"dir." = FIR directo de disenar_PB_PA.m.\n');

pa_eb = res(ismember({res.tipo}, {'PA', 'EB'}));
fprintf('\nModelo muestra a muestra vs vectorizado: error maximo %.1e\n', max([pa_eb.errModelo]));
fprintf('Tonos medidos vs |H| calculada:         error maximo %.1e\n', max([pa_eb.errTonos]));

nMT = taps_cmsis(G(1).q) + taps_cmsis(G(2).q);
for i = 1:nFs
  nMT = nMT + M(i) * ceil(numel(HD(i).q) / M(i)) + taps_cmsis(PB(i).q) + taps_cmsis(LPB(i).q);
end
fprintf('Coeficientes guardados: %d (%.1f KB)', nMT, 2 * nMT / 1024);
if all(isfinite([res.tapsDir]))
  fprintf(' contra %d (%.1f KB) del FIR directo', sum([res.tapsDir]), 2 * sum([res.tapsDir]) / 1024);
end
fprintf('\n');
if ~all([res.ok])
  warning('Hay filtros que NO cumplen (ver columna "cumple"). Ajustar MARGEN, HD_ATEN o HD_RIZADO.');
end
fprintf('Tiempo total: %.1f s\n', toc(tTotal));

%% ======================= SALIDAS =======================
escribir_header(ARCHIVO_H, FS, FS_NOMBRE, M, F_BAJA, G, HD, PB, LPB, res);
escribir_csv(ARCHIVO_CSV, res);
save(ARCHIVO_MAT, 'res', 'tipos', 'FS', 'FS_NOMBRE', 'F_BAJA', 'M', 'G', 'HD', 'PB', ...
     'LPB', 'APASS');
assignin('base', 'filtros_multitasa', res);
fprintf('\nEscritos: %s, %s y %s en %s\n', ARCHIVO_H, ARCHIVO_CSV, ARCHIVO_MAT, pwd);

if graficar
  graficar_respuestas(res, tipos, FS, FS_NOMBRE, dp);
end
end

%% ======================================================================
%  Diseno de un bloque
%% ======================================================================
function r = orden_minimo(c, fs)
  % Menor orden PAR (longitud impar: fase lineal tipo I, retardo entero)
  % cuyos coeficientes redondeados a Q15 cumplen la especificacion
  n0 = firpmord(c.f, c.a, c.dev, fs);
  N  = max(4, 2 * floor(0.85 * n0 / 2));       % arranca debajo de la estimacion
  fo = [0 c.f fs/2] / (fs/2);
  ao = reshape([c.a; c.a], 1, []);
  w  = max(c.dev) ./ c.dev;
  while true
    h = firpm(N, fo, ao, w);
    h = (h + fliplr(h)) / 2;                   % simetria exacta antes de redondear
    q = round(h * 32768);
    if max(abs(q)) > 32767
      error('%s: un coeficiente no entra en Q15.', c.nombre);
    end
    if medir_bloque(q / 32768, c, fs), break; end
    N = N + 2;
    if N > 2 * n0 + 100
      error('%s a %d Hz: ningun orden hasta %d cumple en Q15.', c.nombre, fs, N);
    end
  end
  r = struct('nombre', c.nombre, 'fs', fs, 'N', N, 'N0', n0, 'h', h, 'q', q, 'spec', c);
end

function ok = medir_bloque(h, c, fs)
  % Respuesta densa (32 puntos por lobulo) + bordes exactos
  n = numel(h);
  nfft = max(2^16, 2^nextpow2(32 * n));
  H  = abs(fft(h, nfft));  H = H(1:nfft/2 + 1);
  fr = (0:nfft/2) * fs / nfft;
  bord = [0 c.f fs/2];
  He = abs(exp(-2i * pi * bord(:) * (0:n - 1) / fs) * h(:)).';
  Ha = [H He];  fa = [fr bord];
  ok = true;
  for b = 1:numel(c.a)
    s = fa >= bord(2*b - 1) - 1e-9 & fa <= bord(2*b) + 1e-9;
    if c.a(b) == 1, err = max(abs(Ha(s) - 1)); else, err = max(Ha(s)); end
    ok = ok && err <= c.dev(b);
  end
  ok = ok && max(Ha) <= 1 + max(c.dev(c.a == 1));   % sin sobrepicos en transiciones
end

function n = taps_cmsis(q)
  % arm_fir_q15 pide numTaps par
  n = numel(q) + mod(numel(q), 2);
end

function imprimir_bloque(r)
  fprintf('%-6s %6d %12d %8d %6d\n', r.nombre, r.fs, r.N0, r.N, numel(r.q));
end

%% ======================================================================
%  Analisis del filtro compuesto
%% ======================================================================
function R = analizar(t, fs, M, hd, g, post, dp, espMaxDB)
  % Parte LTI exacta (hmain) y nivel total de las componentes espurias que
  % agrega el cambio de tasa. La grilla tiene F_BAJA = K bins exactos.
  K    = 2^ceil(log2(2^19 / M));
  NFFT = M * K;
  idx  = 1:NFFT/2 + 1;
  fr   = (idx - 1) * fs / NFFT;
  if isempty(g)                                % FIR directo
    hmain = post;
    esp   = zeros(size(idx));
  else
    Nd = numel(hd);  Ng = numel(g);
    D  = (Nd - 1) + M * (Ng - 1) / 2;
    gup = zeros(1, M * (Ng - 1) + 1);  gup(1:M:end) = g;     % G(z^M)
    % Parte LTI: (1/M) * HD * G(z^M) * (M*HD), y el complemento x[n-D] - v
    hmain = -conv(conv(hd, gup), hd);
    hmain(D + 1) = hmain(D + 1) + 1;
    % Espurias: un tono en f sale tambien en f + k*F_BAJA (k ~= 0) con
    % amplitud |HD(f) G(f)| * |HD(f + k*F_BAJA)|. Se suman en potencia.
    Bf = abs(fft(hd, NFFT) .* fft(gup, NFFT));
    C2 = abs(fft(hd, NFFT)).^2;
    if ~isempty(post)
      C2    = C2 .* abs(fft(post, NFFT)).^2;
      hmain = conv(post, hmain);
    end
    T   = sum(reshape(C2, K, M), 2).';
    esp = Bf(idx) .* sqrt(max(T(mod(idx - 1, K) + 1) - C2(idx), 0));
  end
  A = abs(fft(hmain, NFFT));  A = A(idx);

  bord = [0 t.f fs/2];
  Ae = abs(exp(-2i * pi * bord(:) * (0:numel(hmain) - 1) / fs) * hmain(:)).';
  Aa = [A Ae];  fa = [fr bord];
  vmin = inf;  vmax = 0;  smax = 0;
  for b = 1:numel(t.a)
    s = fa >= bord(2*b - 1) - 1e-9 & fa <= bord(2*b) + 1e-9;
    if t.a(b) == 1
      vmin = min(vmin, min(Aa(s)));  vmax = max(vmax, max(Aa(s)));
    else
      smax = max(smax, max(Aa(s)));
    end
  end
  gmax = max(Aa);

  R.hmain   = hmain;
  R.retardo = (numel(hmain) - 1) / 2;          % retardo de grupo total [muestras]
  R.rip     = 20 * log10(vmax / vmin);
  R.att     = -20 * log10(smax);
  R.gmax    = 20 * log10(gmax);
  R.espDB   = 20 * log10(max(esp) + 1e-12);
  if isempty(g), R.espDB = -Inf; end
  R.ok = vmax <= 1 + dp && vmin >= 1 - dp && smax <= 10^(-t.Astop/20) && ...
         gmax <= 1 + dp && R.espDB <= espMaxDB;
  % Para graficar: grilla logaritmica (no se guarda la FFT entera)
  fPlot = logspace(0, log10(fs/2), 4000);
  j = min(round(fPlot * NFFT / fs) + 1, numel(A));
  R.fPlot = fPlot;  R.APlot = A(j);  R.espPlot = esp(j);
end

%% ======================================================================
%  Modelos en el tiempo
%% ======================================================================
function y = cadena_vectorizada(x, hd, g, M, D)
  u  = filter(hd, 1, x);                       % HD a fs
  w  = filter(g, 1, u(1:M:end));               % diezmado (n = 0, M, 2M...) y G
  up = zeros(size(x));  up(1:M:end) = w;       % insercion de ceros
  v  = M * filter(hd, 1, up);                  % interpolador = M * HD
  y  = [zeros(1, D), x(1:end - D)] - v;
end

function y = cadena_muestra_a_muestra(x, hd, g, M, D)
  % Lo que hay que escribir en C, dentro de la ISR del ADC: una muestra por
  % llamada, buffers circulares, sin bloques.
  Nd = numel(hd);  Ng = numel(g);
  Np = M * ceil(Nd / M);
  hp = [hd, zeros(1, Np - Nd)];                % HD rellenado (polifasico)
  Lx = max(D, Nd - 1) + 1;  xb = zeros(1, Lx); % historia de x a fs
  ub = zeros(1, Ng);  wb = zeros(1, Np / M);   % historias a F_BAJA
  y  = zeros(size(x));
  j  = 0:Nd - 1;
  for n = 0:numel(x) - 1
    xb(mod(n, Lx) + 1) = x(n + 1);
    if mod(n, M) == 0                          % instante de diezmado
      u  = hd * xb(mod(n - j, Lx) + 1).';      % HD: solo 1 de cada M muestras
      ub = [u, ub(1:end - 1)];
      wb = [g * ub.', wb(1:end - 1)];          % G a F_BAJA; wb(1) = el mas nuevo
    end
    p = mod(n, M);                             % fase del interpolador
    v = M * (hp(p + 1:M:end) * wb.');
    y(n + 1) = xb(mod(n - D, Lx) + 1) - v;
  end
end

function [eMod, eTon] = probar_en_el_tiempo(hd, g, M, fs, hmain, fTest)
  Nd = numel(hd);  Ng = numel(g);
  D  = (Nd - 1) + M * (Ng - 1) / 2;
  % 1) El modelo muestra a muestra tiene que dar lo mismo que el vectorizado
  s = RandStream('mt19937ar', 'Seed', 1);
  x = 0.1 * randn(s, 1, 2 * (2*D + 1) + 4*M);
  eMod = max(abs(cadena_vectorizada(x, hd, g, M, D) - ...
                 cadena_muestra_a_muestra(x, hd, g, M, D)));
  % 2) Amplitud de tonos en regimen (1 s despues del transitorio) contra
  %    la respuesta calculada
  n  = 0:(2*D + fs);
  ss = 2*D + 1:numel(n);
  eTon = 0;
  for f = fTest
    y   = cadena_vectorizada(cos(2*pi*f*n/fs), hd, g, M, D);
    E   = [cos(2*pi*f*n(ss)/fs); sin(2*pi*f*n(ss)/fs)].';
    amp = norm(E \ y(ss).');
    Hc  = abs(exp(-2i*pi*f*(0:numel(hmain) - 1)/fs) * hmain(:));
    eTon = max(eTon, abs(amp - Hc));
  end
end

%% ======================================================================
%  Salidas
%% ======================================================================
function T = leer_directo(archivo)
  % Taps del FIR directo de disenar_PB_PA.m, clave '<fs>_<filtro>'
  T = containers.Map();
  if ~exist(archivo, 'file'), return; end
  L = strsplit(fileread(archivo), {'\r\n', '\n'});
  for j = 2:numel(L)
    c = strsplit(L{j}, ';');
    if numel(c) >= 10, T(sprintf('%s_%s', c{1}, c{2})) = str2double(c{10}); end
  end
end

function escribir_header(archivo, FS, FS_NOMBRE, M, F_BAJA, G, HD, PB, LPB, res)
  nFs = numel(FS);
  cm  = @(q) [zeros(1, mod(numel(q), 2)), fliplr(q)];   % CMSIS, 0 al principio
  Np  = arrayfun(@(i) M(i) * ceil(numel(HD(i).q) / M(i)), 1:nFs);
  Dpa = arrayfun(@(i) (numel(HD(i).q) - 1) + M(i) * (numel(G(1).q) - 1) / 2, 1:nFs);
  Deb = arrayfun(@(i) (numel(HD(i).q) - 1) + M(i) * (numel(G(2).q) - 1) / 2, 1:nFs);
  Lx  = max([Dpa, Deb, arrayfun(@(r) numel(r.q) - 1, HD)]) + 1;

  fid = fopen(archivo, 'w');
  if fid < 0, error('No se pudo abrir %s para escribir', archivo); end
  fprintf(fid, '/*\n * %s - generado por disenar_multitasa.m (%s)\n', archivo, datestr(now));
  fprintf(fid, [ ...
    ' * Filtros FIR multitasa en Q15. Reemplaza a FiltrosTP2.h: no incluir los\n' ...
    ' * dos. Incluir SOLO desde un .c (los arreglos son static const).\n' ...
    ' *\n' ...
    ' * Estructura (M = fs / F_BAJA_HZ):\n' ...
    ' *   PB     : arm_fir_q15 con PB_<fs> (FIR directo a fs).\n' ...
    ' *   PA, EB : y[n] = x[n - D] - v[n], con D de d_pa_tabla / d_eb_tabla.\n' ...
    ' *            v[n] es el camino angosto:\n' ...
    ' *              si n %% M == 0: u = (sum_{j=0}^{NUM_TAPS_HD-1} HD[j] * x[n-j]) >> 15\n' ...
    ' *                             w = G(u), arm_fir_q15 de 1 muestra (G_PA o G_EB)\n' ...
    ' *                             wb[] <- w   (historia, wb[0] = el mas nuevo)\n' ...
    ' *              siempre:       p = n %% M\n' ...
    ' *                             v = (M * sum_{m=0}^{HD_PAD/M-1} HD[p + m*M] * wb[m]) >> 15\n' ...
    ' *            La historia de x tiene que guardar LONG_HIST_X muestras.\n' ...
    ' *            Para bajar el pico, la suma de HD se puede repartir: cada x[n]\n' ...
    ' *            que llega suma sus ceil(NUM_TAPS_HD/M) productos a los\n' ...
    ' *            acumuladores de los diezmados pendientes (mismo u, mismo D).\n' ...
    ' *   PBanda : PA y, a su salida, arm_fir_q15 con LPB_<fs> (cascada).\n' ...
    ' *\n' ...
    ' * Formatos (todos simetricos, de fase lineal):\n' ...
    ' *   HD_<fs> : orden natural, NUM_TAPS_HD impar, rellenado con ceros hasta\n' ...
    ' *             HD_PAD (multiplo de M) para indexar el polifasico sin chequeos.\n' ...
    ' *             El mismo HD es diezmador e interpolador (este con ganancia M).\n' ...
    ' *   G_PA, G_EB, PB_<fs>, LPB_<fs> : formato CMSIS (orden invertido) con un\n' ...
    ' *             0 al PRINCIPIO, para que numTaps sea par sin agregar retardo\n' ...
    ' *             (en G un retardo extra rompe el complemento x[n-D] - v[n]).\n' ...
    ' * Referencia exacta: cadena_muestra_a_muestra() en disenar_multitasa.m.\n' ...
    ' *\n' ...
    ' *  fs     filtro   MAC pico  MAC prom  retardo[ms]  rizado[dB]  aten[dB]  espurias[dB]\n']);
  for r = res
    fprintf(fid, ' *  %-6s %-7s %8.0f  %8.1f  %11.1f  %10.3f  %8.2f  %12.1f\n', r.fsNombre, ...
            r.tipo, r.pico, r.prom, 1000 * r.retardo / r.fs, r.rip, r.att, r.espDB);
  end
  fprintf(fid, ' */\n\n#ifndef FILTROS_MULTITASA_H_\n#define FILTROS_MULTITASA_H_\n\n');
  fprintf(fid, '#include "arm_math.h"\n\n');
  fprintf(fid, '#define NUM_FS        %d\n#define NUM_TIPOS     4\n', nFs);
  fprintf(fid, '#define F_BAJA_HZ     %d\n', F_BAJA);
  fprintf(fid, '#define NUM_TAPS_G_PA %d\n#define NUM_TAPS_G_EB %d\n', ...
          numel(cm(G(1).q)), numel(cm(G(2).q)));
  fprintf(fid, '#define MAX_HD_PAD    %d\n', max(Np));
  fprintf(fid, '#define MAX_HIST_W    %d   /* max(HD_PAD / M): historia de w */\n', max(Np ./ M));
  fprintf(fid, '#define LONG_HIST_X   %d   /* max(D, NUM_TAPS_HD - 1) + 1 */\n', Lx);
  fprintf(fid, '#define MAX_TAPS_FS   %d   /* PB y LPB, para el estado de arm_fir_q15 */\n\n', ...
          max([arrayfun(@(r) numel(cm(r.q)), PB), arrayfun(@(r) numel(cm(r.q)), LPB)]));
  fprintf(fid, 'enum { PB = 0, PA, PBANDA, EB };\n');
  fprintf(fid, 'enum { K8 = 0, K16, K22, K44, K48 };\n\n');
  fprintf(fid, 'static const uint32_t fs_hz[NUM_FS] = {%s};\n', lista(FS));
  fprintf(fid, 'static const char * const nombre_tipo[NUM_TIPOS] = {"PB", "PA", "PBanda", "EB"};\n');
  fprintf(fid, 'static const uint8_t  m_tabla[NUM_FS]    = {%s};\n', lista(M));
  fprintf(fid, 'static const uint16_t d_pa_tabla[NUM_FS] = {%s};\n', lista(Dpa));
  fprintf(fid, 'static const uint16_t d_eb_tabla[NUM_FS] = {%s};\n\n', lista(Deb));

  escribir_arreglo(fid, 'G_PA', 'NUM_TAPS_G_PA', cm(G(1).q), ...
    sprintf('G_PA: pasa bajos a %d Hz, orden %d (CMSIS)', F_BAJA, G(1).N));
  escribir_arreglo(fid, 'G_EB', 'NUM_TAPS_G_EB', cm(G(2).q), ...
    sprintf('G_EB: pasa banda a %d Hz, orden %d (CMSIS)', F_BAJA, G(2).N));
  for i = 1:nFs
    s = upper(FS_NOMBRE{i});
    fprintf(fid, '/* ---------------- fs = %d Hz (M = %d) ---------------- */\n', FS(i), M(i));
    fprintf(fid, '#define NUM_TAPS_HD_%s %d\n#define HD_PAD_%s %d\n', s, numel(HD(i).q), s, Np(i));
    escribir_arreglo(fid, ['HD_' FS_NOMBRE{i}], ['HD_PAD_' s], ...
      [HD(i).q, zeros(1, Np(i) - numel(HD(i).q))], ...
      sprintf('HD: diezmador / interpolador, orden %d (orden natural)', HD(i).N));
    fprintf(fid, '#define NUM_TAPS_PB_%s %d\n', s, numel(cm(PB(i).q)));
    escribir_arreglo(fid, ['PB_' FS_NOMBRE{i}], ['NUM_TAPS_PB_' s], cm(PB(i).q), ...
      sprintf('PB directo, orden %d (CMSIS)', PB(i).N));
    fprintf(fid, '#define NUM_TAPS_LPB_%s %d\n', s, numel(cm(LPB(i).q)));
    escribir_arreglo(fid, ['LPB_' FS_NOMBRE{i}], ['NUM_TAPS_LPB_' s], cm(LPB(i).q), ...
      sprintf('LPB: flanco superior del PBanda, orden %d (CMSIS)', LPB(i).N));
  end

  noms = @(p) strjoin(cellfun(@(x) [p x], FS_NOMBRE, 'UniformOutput', false), ', ');
  defs = @(p) strjoin(cellfun(@(x) [p upper(x)], FS_NOMBRE, 'UniformOutput', false), ', ');
  fprintf(fid, 'static const q15_t * const hd_tabla[NUM_FS]  = {%s};\n', noms('HD_'));
  fprintf(fid, 'static const uint16_t hd_taps[NUM_FS]        = {%s};\n', defs('NUM_TAPS_HD_'));
  fprintf(fid, 'static const uint16_t hd_pad[NUM_FS]         = {%s};\n', defs('HD_PAD_'));
  fprintf(fid, 'static const q15_t * const pb_tabla[NUM_FS]  = {%s};\n', noms('PB_'));
  fprintf(fid, 'static const uint16_t pb_taps[NUM_FS]        = {%s};\n', defs('NUM_TAPS_PB_'));
  fprintf(fid, 'static const q15_t * const lpb_tabla[NUM_FS] = {%s};\n', noms('LPB_'));
  fprintf(fid, 'static const uint16_t lpb_taps[NUM_FS]       = {%s};\n', defs('NUM_TAPS_LPB_'));
  fprintf(fid, '\n#endif /* FILTROS_MULTITASA_H_ */\n');
  fclose(fid);
end

function s = lista(v)
  s = strjoin(arrayfun(@(x) sprintf('%d', x), v, 'UniformOutput', false), ', ');
end

function escribir_arreglo(fid, nombre, ndef, q, comentario)
  fprintf(fid, '/* %s */\n', comentario);
  fprintf(fid, 'static const q15_t %s[%s] = {\n', nombre, ndef);
  for p = 1:12:numel(q)
    fila = q(p:min(p + 11, numel(q)));
    txt = sprintf('%d, ', fila);
    if p + 11 >= numel(q), txt = txt(1:end - 2); end
    fprintf(fid, '  %s\n', strtrim(txt));
  end
  fprintf(fid, '};\n\n');
end

function escribir_csv(archivo, res)
  fid = fopen(archivo, 'w');
  if fid < 0, error('No se pudo abrir %s para escribir', archivo); end
  % Separador ';' y coma decimal, para abrirlo directo en Excel en espanol
  fprintf(fid, ['fs [Hz];Filtro;Factor M;MAC pico por muestra;MAC pico con HD repartido;' ...
                'MAC promedio por muestra;CPU pico [%%];CPU pico con HD repartido [%%];' ...
                'CPU promedio [%%];Taps FIR directo;CPU FIR directo [%%];' ...
                'Retardo de grupo [muestras];Retardo de grupo [ms];Rizado [dB];' ...
                'Atenuacion [dB];Ganancia maxima [dB];Espurias [dB];Cumple\n']);
  siNo = {'no', 'si'};
  for r = res
    lin = sprintf('%d;%s;%d;%.0f;%.0f;%.2f;%.1f;%.1f;%.1f;%g;%.1f;%g;%.2f;%.3f;%.2f;%.3f;%.1f;%s', ...
                  r.fs, r.tipo, r.M, r.pico, r.picoRep, r.prom, r.cpuPico, r.cpuPicoRep, ...
                  r.cpuProm, r.tapsDir, ...
                  r.cpuDir, r.retardo, 1000 * r.retardo / r.fs, r.rip, r.att, r.gmax, ...
                  r.espDB, siNo{r.ok + 1});
    fprintf(fid, '%s\n', strrep(lin, '.', ','));
  end
  fclose(fid);
end

function graficar_respuestas(res, tipos, FS, FS_NOMBRE, dp)
  for k = 1:numel(tipos)
    figure('Name', ['Multitasa ' tipos(k).nombre ' (Q15)'], 'NumberTitle', 'off');
    hold on; grid on;
    ley = {};
    for i = 1:numel(FS)
      r = res((i - 1) * numel(tipos) + k);
      plot(r.fPlot, 20 * log10(r.APlot + 1e-12));
      ley{end + 1} = sprintf('%s (CPU pico %.0f%%)', FS_NOMBRE{i}, r.cpuPico); %#ok<AGROW>
    end
    t = tipos(k);
    ejes = [1 max(FS)/2 -70 5];
    plot(ejes(1:2), -t.Astop * [1 1], 'k--', ejes(1:2), 20*log10(1 - dp) * [1 1], 'k:');
    ley = [ley, {sprintf('-%d dB (Astop)', t.Astop), 'limite banda de paso'}]; %#ok<AGROW>
    set(gca, 'XScale', 'log');
    axis(ejes);
    xlabel('Frecuencia [Hz]'); ylabel('|H| [dB]');
    title(sprintf('%s multitasa - bordes [%s] Hz - coeficientes Q15', t.nombre, ...
          strtrim(sprintf('%g ', t.f))));
    legend(ley, 'Location', 'eastoutside');
  end
end
