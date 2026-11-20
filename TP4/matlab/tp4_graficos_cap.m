function figs = tp4_graficos_cap(datos, titulo, carpeta, prefijo)
%TP4_GRAFICOS_CAP Gráficos de una captura de formas de onda de la Parte 2.
%
%   tp4_graficos_cap(datos)                     % datos = tp4_parse_log(...)
%   tp4_graficos_cap('p2_captura.txt', '', 'img', 'p2_cap')
%
%   Usa la última captura de datos.cap (comando K<n> o Z<n> de la placa):
%     1) señal + ruido d(n), salida del cancelador e(n) y señal limpia s(n)
%     2) ruido que contamina la señal vs. su estimación y(n), y residuo e - s
%     3) espectro de d(n) y de e(n) (cuánto ruido se sacó en cada frecuencia)
%   Si la captura arrancó junto con el reinicio de los coeficientes (Z<n>),
%   el gráfico 1 muestra el transitorio de aprendizaje.

    if ischar(datos)
        datos = tp4_parse_log(datos);
    end
    if nargin < 2, titulo = ''; end
    if nargin < 3, carpeta = ''; end
    if nargin < 4, prefijo = 'p2_cap'; end
    if isempty(datos.cap)
        error('No hay capturas en los datos (comando K<n> o Z<n>)');
    end

    c = datos.cap(end);
    fs = c.cfg.fs;
    t_ms = 1e3 * c.t;
    ruido = c.d - c.s;           % ruido que se le sumó a la señal
    figs = {};

    sub = sprintf('\\mu = %g, \\sigma = %g V, %s', c.cfg.mu, c.cfg.ruido, lower(c.cfg.topo));

    % 1) Formas de onda
    f = figure('Name', 'P2 - formas de onda', 'Color', 'w');
    plot(t_ms, c.d, 'Color', [0.75 0.75 0.75]); hold on;
    plot(t_ms, c.e, 'b', 'LineWidth', 1.1);
    plot(t_ms, c.s, 'k--', 'LineWidth', 1.0);
    hold off; grid on;
    xlabel('tiempo (ms)'); ylabel('V (sin continua)');
    legend({'d = s + ruido', 'e (salida del cancelador)', 's (generador)'}, 'Location', 'northeast');
    title(strtrim(['Cancelación de ruido ' titulo ' - ' sub]), 'Interpreter', 'tex');
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_ondas']);

    % 2) Ruido y su estimación
    f = figure('Name', 'P2 - ruido estimado', 'Color', 'w');
    subplot(2, 1, 1);
    plot(t_ms, ruido, 'Color', [0.6 0.6 0.6]); hold on;
    plot(t_ms, c.y, 'r');
    hold off; grid on;
    ylabel('V'); legend({'ruido en d', 'y (estimación)'}, 'Location', 'northeast');
    title('El filtro adaptativo reconstruye el ruido');
    subplot(2, 1, 2);
    plot(t_ms, c.e - c.s, 'm'); grid on;
    xlabel('tiempo (ms)'); ylabel('e - s (V)');
    title('Ruido residual');
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_ruido']);

    % 3) Espectros (sólo la segunda mitad: ya convergido si arrancó con Z)
    n = numel(c.d);
    idx = floor(n/2)+1:n;
    if numel(idx) >= 64
        f = figure('Name', 'P2 - espectros', 'Color', 'w');
        [Pd, fr] = espectro(c.d(idx), fs);
        Pe = espectro(c.e(idx), fs);
        plot(fr, 10*log10(Pd), 'Color', [0.6 0.6 0.6]); hold on;
        plot(fr, 10*log10(Pe), 'b');
        hold off; grid on;
        xlabel('frecuencia (Hz)'); ylabel('dB (V^2/Hz)');
        legend({'d = s + ruido', 'e (salida)'}, 'Location', 'northeast');
        title('Densidad espectral (segunda mitad de la captura)');
        figs{end+1} = f;
        guardar(f, carpeta, [prefijo '_espectro']);
    end

    % Resumen en la consola
    seg = idx;
    fprintf('\nCaptura de %d muestras a %g Hz (%s)\n', n, fs, sub);
    fprintf('  potencia del ruido en d: %.3e V^2\n', mean(ruido(seg).^2));
    fprintf('  potencia del residuo e-s: %.3e V^2  -> reducción %.1f dB\n', ...
        mean((c.e(seg) - c.s(seg)).^2), 10*log10(mean(ruido(seg).^2) / mean((c.e(seg) - c.s(seg)).^2)));
end

function [P, fr] = espectro(x, fs)
    % Periodograma promediado (Welch con ventana de Hann), sin toolboxes
    x = x(:) - mean(x);
    N = 256;
    if numel(x) < N, N = 2^floor(log2(numel(x))); end
    w = 0.5 - 0.5*cos(2*pi*(0:N-1)'/(N-1));
    paso = N/2;
    nseg = floor((numel(x) - N)/paso) + 1;
    P = zeros(N/2+1, 1);
    for k = 0:nseg-1
        seg = x(k*paso + (1:N)) .* w;
        X = fft(seg);
        P = P + abs(X(1:N/2+1)).^2;
    end
    P = P / (nseg * fs * sum(w.^2));
    P(2:end-1) = 2*P(2:end-1);
    fr = (0:N/2)' * fs / N;
end

function guardar(f, carpeta, nombre)
    if isempty(carpeta)
        return;
    end
    if ~exist(carpeta, 'dir')
        mkdir(carpeta);
    end
    print(f, fullfile(carpeta, [nombre '.png']), '-dpng', '-r150');
    try
        print(f, fullfile(carpeta, [nombre '.pdf']), '-dpdf');
    catch
        % (Octave sin Ghostscript no exporta PDF; el PNG alcanza)
    end
end
