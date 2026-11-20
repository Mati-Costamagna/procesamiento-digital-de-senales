function datos = tp4_parse_log(entrada)
%TP4_PARSE_LOG Decodifica el texto que manda la placa (o la simulación).
%
%   datos = tp4_parse_log('archivo.txt')
%   datos = tp4_parse_log(lineas)        % cell array de líneas
%
%   datos.p1(i)  una corrida de la Parte 1 (identificación de planta):
%       .cfg     parámetros (alg, mu, in, A, Px, L, N, blk, sv, seed, mu_max, tau)
%       .planta  coeficientes de la planta (1xL, L = 10)
%       .n       índice de muestra al final de cada bloque de 100
%       .mse     MSE promedio de cada bloque
%       .mse_dB  10 log10(mse)
%       .werr2   ||p - w||^2 al final de cada bloque
%       .diff    p - w de cada coeficiente (una fila por bloque)
%       .w       coeficientes finales del filtro adaptativo
%       .fin     n, mse, werr2, cyc (ciclos por muestra del LMS), estado
%
%   datos.p2(i)  un tramo de la Parte 2 (cancelador), desde cada encabezado P2I:
%       .cfg     parámetros (alg, mu, ruido, topo, fs, blk, L, Px, mu_max, tau)
%       .n, .t   muestra y tiempo (s) al final de cada bloque de 100
%       .p_ruido, .p_res, .red_dB, .werr2, .snr_in_dB, .snr_out_dB
%       .w, .w_opt, .c   últimos coeficientes pedidos con el comando C
%                        (filtro, óptimo de Wiener y correlador)
%       .tiempos     línea P2T (ciclos por muestra)
%
%   datos.cap(i) una captura de formas de onda (comando K<n> o Z<n>):
%       .cfg     n, fs, mu, ruido, topo
%       .t       tiempo (s) desde la primera muestra
%       .d .e .y .s .x   señal + ruido, salida, ruido estimado, señal y referencia (V)
%
%
%   Las líneas que no reconoce (ayuda, ecos, etc.) se ignoran.

    if ischar(entrada) && exist(entrada, 'file') == 2
        txt = fileread(entrada);
        lineas = regexp(txt, '\r?\n', 'split');
    elseif ischar(entrada)
        lineas = regexp(entrada, '\r?\n', 'split');
    else
        lineas = cellstr(entrada);
    end

    datos.p1 = repmat(nueva_p1(), 0, 1);
    datos.p2 = repmat(nueva_p2(), 0, 1);
    datos.cap = repmat(nueva_cap(), 0, 1);
    cur1 = [];
    cur2 = [];
    curc = [];
    filas = [];

    for i = 1:numel(lineas)
        ln = strtrim(char(lineas{i}));
        if isempty(ln)
            continue;
        end
        coma = find(ln == ',', 1);
        if isempty(coma)
            tag = ln;
            resto = '';
        else
            tag = ln(1:coma-1);
            resto = ln(coma+1:end);
        end

        switch tag
            % ---------------- Parte 1 ----------------
            case 'P1I'
                if ~isempty(cur1)
                    datos.p1(end+1, 1) = cur1;   % corrida sin P1F (cortada)
                end
                cur1 = nueva_p1();
                cur1.cfg = leer_kv(resto);
            case 'P1P'
                if ~isempty(cur1), cur1.planta = numeros(resto); end
            case 'P1M'
                if ~isempty(cur1)
                    v = numeros(resto);          % n, mse, ||p - w||^2
                    if numel(v) >= 3
                        cur1.n(end+1, 1) = v(1);
                        cur1.mse(end+1, 1) = v(2);
                        cur1.werr2(end+1, 1) = v(3);
                        cur1.mse_dB(end+1, 1) = 10*log10(max(v(2), realmin));
                    end
                end
            case 'P1D'
                if ~isempty(cur1)
                    v = numeros(resto);
                    cur1.n_diff(end+1, 1) = v(1);
                    cur1.diff(end+1, :) = v(2:end);
                end
            case 'P1W'
                if ~isempty(cur1), cur1.w = numeros(resto); end
            case 'P1F'
                if ~isempty(cur1)
                    p = strsplit(resto, ',');
                    cur1.fin = struct('n', str2double(p{1}), 'mse', str2double(p{2}), ...
                        'werr2', str2double(p{3}), 'cyc', str2double(p{4}), 'estado', p{5});
                    datos.p1(end+1, 1) = cur1;
                    cur1 = [];
                end

            % ---------------- Parte 2 ----------------
            case 'P2I'
                if ~isempty(cur2)
                    datos.p2(end+1, 1) = cur2;
                end
                cur2 = nueva_p2();
                cur2.cfg = leer_kv(resto);
            case 'P2M'
                if ~isempty(cur2)
                    v = numeros(resto);          % n, p_ruido, p_residuo, p_senal, werr2
                    if numel(v) >= 5
                        cur2.n(end+1, 1) = v(1);
                        cur2.p_ruido(end+1, 1) = v(2);
                        cur2.p_res(end+1, 1) = v(3);
                        cur2.red_dB(end+1, 1) = 10*log10(v(2) / max(v(3), realmin));
                        cur2.werr2(end+1, 1) = v(5);
                        cur2.snr_in_dB(end+1, 1) = 10*log10(v(4) / max(v(2), realmin));
                        cur2.snr_out_dB(end+1, 1) = 10*log10(v(4) / max(v(3), realmin));
                    end
                end
            case 'P2W'
                if ~isempty(cur2), cur2.w = numeros(resto); end
            case 'P2O'
                if ~isempty(cur2), cur2.w_opt = numeros(resto); end
            case 'P2C'
                if ~isempty(cur2), cur2.c = numeros(resto); end
            case 'P2T'
                if ~isempty(cur2), cur2.tiempos = leer_kv(resto); end

            % ---------------- Capturas de formas de onda ----------------
            case 'CAPI'
                curc = nueva_cap();
                curc.cfg = leer_kv(resto);
                filas = nan(max(curc.cfg.n, 1), 6);
            case 'CAP'
                if ~isempty(curc)
                    v = numeros(resto);
                    if numel(v) >= 6 && v(1) + 1 <= size(filas, 1)
                        filas(v(1) + 1, :) = v(1:6);
                    end
                end
            case 'CAPF'
                if ~isempty(curc)
                    datos.cap(end+1, 1) = cerrar_cap(curc, filas);
                    curc = [];
                end
        end
    end

    if ~isempty(cur1)
        datos.p1(end+1, 1) = cur1;
    end
    if ~isempty(cur2)
        datos.p2(end+1, 1) = cur2;
    end
    if ~isempty(curc)
        datos.cap(end+1, 1) = cerrar_cap(curc, filas);   % captura cortada
    end

    % El primer índice de muestra de cada tramo de la Parte 2 arranca en 0
    for k = 1:numel(datos.p2)
        if isfield(datos.p2(k).cfg, 'fs') && ~isempty(datos.p2(k).n)
            n0 = datos.p2(k).n(1) - datos.p2(k).cfg.blk;
            datos.p2(k).t = (datos.p2(k).n - n0) / datos.p2(k).cfg.fs;
        end
    end
end

% -------------------------------------------------------------------------
function s = nueva_p1()
    s = struct('cfg', struct(), 'planta', [], 'n', zeros(0,1), 'mse', zeros(0,1), ...
        'werr2', zeros(0,1), 'mse_dB', zeros(0,1), 'n_diff', zeros(0,1), ...
        'diff', zeros(0,0), 'w', [], 'fin', struct('n', NaN, 'mse', NaN, ...
        'werr2', NaN, 'cyc', NaN, 'estado', 'CORTADO'));
end

function s = nueva_p2()
    s = struct('cfg', struct(), 'n', zeros(0,1), 't', zeros(0,1), 'p_ruido', zeros(0,1), ...
        'p_res', zeros(0,1), 'red_dB', zeros(0,1), 'werr2', zeros(0,1), ...
        'snr_in_dB', zeros(0,1), 'snr_out_dB', zeros(0,1), 'w', [], 'w_opt', [], ...
        'c', [], 'tiempos', struct());
end

function s = nueva_cap()
    s = struct('cfg', struct(), 't', zeros(0,1), 'd', zeros(0,1), 'e', zeros(0,1), ...
        'y', zeros(0,1), 's', zeros(0,1), 'x', zeros(0,1));
end

function c = cerrar_cap(c, filas)
    ok = ~isnan(filas(:, 1));
    filas = filas(ok, :);
    fs = 8000;
    if isfield(c.cfg, 'fs'), fs = c.cfg.fs; end
    c.t = filas(:, 1) / fs;
    c.d = filas(:, 2);
    c.e = filas(:, 3);
    c.y = filas(:, 4);
    c.s = filas(:, 5);
    c.x = filas(:, 6);
end

function v = numeros(resto)
    v = str2double(strsplit(resto, ','));
end

function s = leer_kv(resto)
    % "k1=v1,k2=v2,..." -> struct; los valores numéricos se convierten
    s = struct();
    partes = strsplit(resto, ',');
    for i = 1:numel(partes)
        kv = strsplit(partes{i}, '=');
        if numel(kv) ~= 2
            continue;
        end
        clave = matlab.lang.makeValidName(strtrim(kv{1}));
        num = str2double(kv{2});
        if isnan(num) && ~strcmpi(strtrim(kv{2}), 'nan')
            s.(clave) = strtrim(kv{2});
        else
            s.(clave) = num;
        end
    end
end
