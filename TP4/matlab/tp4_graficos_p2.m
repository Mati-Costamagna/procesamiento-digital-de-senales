function figs = tp4_graficos_p2(datos, titulo, carpeta, prefijo)
%TP4_GRAFICOS_P2 Gráficos de la Parte 2 (cancelador adaptativo de ruido).
%
%   tp4_graficos_p2(datos)                         % datos = tp4_parse_log(...)
%   tp4_graficos_p2('p2_mu.txt', '', 'img', 'p2_mu')
%
%   Usa todos los tramos de datos.p2 (cada encabezado P2I es un tramo):
%     1) reducción del ruido 10 log10(P_ruido / P_residuo) vs. tiempo
%     2) error de los coeficientes ||w_opt - w||^2 vs. tiempo
%     3) coeficientes del filtro vs. los óptimos (si se pidieron con C)

    if ischar(datos)
        datos = tp4_parse_log(datos);
    end
    if nargin < 2, titulo = ''; end
    if nargin < 3, carpeta = ''; end
    if nargin < 4, prefijo = 'p2'; end

    R = datos.p2;
    R = R(arrayfun(@(r) ~isempty(r.n), R));
    if isempty(R)
        error('No hay datos de la Parte 2');
    end
    ley = arrayfun(@(r) sprintf('\\mu = %g, %s', r.cfg.mu, lower(r.cfg.topo)), R, ...
        'UniformOutput', false);
    figs = {};

    f = figure('Name', 'P2 - reducción de ruido', 'Color', 'w');
    hold on;
    for k = 1:numel(R)
        plot(R(k).t, R(k).red_dB, 'LineWidth', 1.1);
    end
    hold off; grid on;
    xlabel('tiempo (s)'); ylabel('reducción del ruido (dB)');
    title(strtrim(['Reducción de ruido ' titulo]));
    legend(ley, 'Location', 'southeast', 'Interpreter', 'tex');
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_red']);

    f = figure('Name', 'P2 - error de coeficientes', 'Color', 'w');
    hold on;
    for k = 1:numel(R)
        plot(R(k).t, 10*log10(max(R(k).werr2, realmin)), 'LineWidth', 1.1);
    end
    hold off; grid on;
    xlabel('tiempo (s)'); ylabel('||w_{opt} - w||^2 (dB)');
    title(strtrim(['Convergencia al filtro óptimo ' titulo]));
    legend(ley, 'Location', 'northeast', 'Interpreter', 'tex');
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_werr']);

    con_w = find(arrayfun(@(r) ~isempty(r.w) && ~isempty(r.w_opt), R));
    if ~isempty(con_w)
        f = figure('Name', 'P2 - coeficientes', 'Color', 'w');
        r = R(con_w(end));
        k = 0:numel(r.w)-1;
        stem(k, r.w_opt, 'k', 'filled', 'LineWidth', 1.5);
        hold on;
        plot(k, r.w, 'ro--');
        hold off; grid on;
        xlabel('k'); ylabel('coeficiente');
        legend({'óptimo (Wiener)', 'filtro adaptativo'}, 'Location', 'northeast');
        title(sprintf('Coeficientes, \\mu = %g', r.cfg.mu));
        figs{end+1} = f;
        guardar(f, carpeta, [prefijo '_coef']);
    end
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
