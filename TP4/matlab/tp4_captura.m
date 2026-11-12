function datos = tp4_captura(puerto, comandos, fin, archivo, opciones)
%TP4_CAPTURA Manda comandos a la placa y guarda lo que contesta.
%
%   datos = tp4_captura(puerto, comandos, fin, archivo)
%
%   puerto    'COM3' (el MCU-Link VCom Port; ver el Administrador de dispositivos)
%   comandos  cell array de comandos, ej. {'1', 'G1', 'E0', 'A1', 'U0.01', 'S'}
%   fin       cuándo dejar de leer:
%               - texto: se corta al recibir una línea que empieza así
%                 (ej. 'P1F' para una corrida, 'P1B,FIN' para un barrido)
%               - número: segundos de captura (para la Parte 2)
%   archivo   .txt donde se guarda todo lo recibido (opcional, '' = no guarda)
%
%   Durante la captura se grafica en vivo la curva de aprendizaje (Parte 1) o
%   la reducción de ruido (Parte 2). Devuelve lo mismo que tp4_parse_log.
%
%   Ejemplos:
%     d = tp4_captura('COM3', {'1','G1','E0','A1','U0.01','S'}, 'P1F', 'p1_mu0.01.txt');
%     d = tp4_captura('COM3', {'1','G1','E0','A1','B'}, 'P1B,FIN', 'p1_barrido.txt');
%     d = tp4_captura('COM3', {'2','U0.005','Z'}, 10, 'p2_mu0.005.txt');
%
%   Requiere MATLAB R2019b o posterior (serialport).

    if nargin < 4
        archivo = '';
    end
    if nargin < 5
        opciones = struct();
    end
    if ~isfield(opciones, 'baud'), opciones.baud = 115200; end
    if ~isfield(opciones, 'timeout_s'), opciones.timeout_s = 120; end
    if ~isfield(opciones, 'vivo'), opciones.vivo = true; end

    sp = serialport(puerto, opciones.baud, 'Timeout', 2);
    limpiar = onCleanup(@() delete(sp));
    configureTerminator(sp, 'CR/LF', 'LF');
    pause(0.2);
    flush(sp);

    % Si se va a cambiar de parte, primero se manda eso y se espera un poco
    for i = 1:numel(comandos)
        writeline(sp, comandos{i});
        pause(0.15);
    end

    lineas = {};
    t0 = tic;
    if opciones.vivo
        [fig, h] = preparar_grafico();
    end
    n_ult = 0;

    while true
        if isnumeric(fin) && toc(t0) >= fin
            break;
        end
        if toc(t0) > opciones.timeout_s
            warning('tp4_captura: se cumplió el timeout de %g s', opciones.timeout_s);
            break;
        end
        if sp.NumBytesAvailable == 0
            pause(0.01);
            continue;
        end
        ln = readline(sp);
        if isempty(ln)
            continue;
        end
        ln = strtrim(char(ln));
        lineas{end+1, 1} = ln; %#ok<AGROW>

        if opciones.vivo && (numel(lineas) - n_ult >= 10)
            n_ult = numel(lineas);
            actualizar_grafico(fig, h, lineas);
        end
        if ischar(fin) && strncmp(ln, fin, numel(fin))
            break;
        end
    end

    if ~isempty(archivo)
        fid = fopen(archivo, 'w');
        fprintf(fid, '%s\n', lineas{:});
        fclose(fid);
        fprintf('Guardadas %d líneas en %s\n', numel(lineas), archivo);
    end
    datos = tp4_parse_log(lineas);
    if opciones.vivo
        actualizar_grafico(fig, h, lineas);
    end
end

function [fig, h] = preparar_grafico()
    fig = figure('Name', 'TP4 - captura en vivo', 'NumberTitle', 'off');
    h = axes(fig);
    grid(h, 'on');
    xlabel(h, 'muestra n');
    ylabel(h, 'dB');
    title(h, 'Esperando datos...');
end

function actualizar_grafico(fig, h, lineas)
    if ~isvalid(fig)
        return;
    end
    d = tp4_parse_log(lineas);
    cla(h);
    hold(h, 'on');
    if ~isempty(d.p1)
        for k = 1:numel(d.p1)
            r = d.p1(k);
            if ~isempty(r.n)
                plot(h, r.n, r.mse_dB, 'DisplayName', sprintf('\\mu = %g', r.cfg.mu));
            end
        end
        ylabel(h, 'MSE (dB)');
        title(h, 'Parte 1 - curva de aprendizaje');
        legend(h, 'show', 'Location', 'northeast');
    elseif ~isempty(d.p2)
        r = d.p2(end);
        if ~isempty(r.n)
            plot(h, r.n, r.red_dB, 'DisplayName', 'reducción de ruido');
            plot(h, r.n, r.snr_out_dB, 'DisplayName', 'SNR a la salida');
        end
        ylabel(h, 'dB');
        title(h, sprintf('Parte 2 - \\mu = %g', r.cfg.mu));
        legend(h, 'show', 'Location', 'southeast');
    end
    hold(h, 'off');
    drawnow limitrate;
end
