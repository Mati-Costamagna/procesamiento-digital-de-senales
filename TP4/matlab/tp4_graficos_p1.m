function figs = tp4_graficos_p1(datos, titulo, carpeta, prefijo)
%TP4_GRAFICOS_P1 Gráficos de la Parte 1 (identificación de planta).
%
%   tp4_graficos_p1(datos)                       % datos = tp4_parse_log(...)
%   tp4_graficos_p1('p1_barrido.txt', 'Entrada aleatoria A = 1', 'img', 'p1_random')
%
%   Hace 4 figuras con todas las corridas que haya en datos.p1:
%     1) curva de aprendizaje: MSE promediado cada 100 muestras, en dB
%     2) error de los coeficientes ||p - w||^2, en dB
%     3) coeficientes finales del filtro adaptativo vs. los de la planta
%     4) diferencia p_k - w_k de cada coeficiente a lo largo del aprendizaje
%        (para la corrida elegida, por defecto la primera que convergió)
%   Si se da una carpeta, las guarda como .png y .pdf para el informe.

    if ischar(datos)
        datos = tp4_parse_log(datos);
    end
    if nargin < 2, titulo = ''; end
    if nargin < 3, carpeta = ''; end
    if nargin < 4, prefijo = 'p1'; end

    R = datos.p1;
    if isempty(R)
        error('No hay corridas de la Parte 1 en los datos');
    end
    leyendas = arrayfun(@(r) etiqueta(r), R, 'UniformOutput', false);
    figs = {};

    % 1) Curva de aprendizaje
    f = figure('Name', 'P1 - curva de aprendizaje', 'Color', 'w');
    hold on;
    for k = 1:numel(R)
        plot(R(k).n, R(k).mse_dB, 'LineWidth', 1.2);
    end
    hold off; grid on;
    xlabel('muestra n'); ylabel('MSE promedio de 100 muestras (dB)');
    title(strtrim(['Curva de aprendizaje ' titulo]));
    legend(leyendas, 'Location', 'northeast', 'Interpreter', 'tex');
    ylim(limites_dB(vertcat(R.mse_dB)));
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_mse']);

    % 2) Error de los coeficientes
    f = figure('Name', 'P1 - error de coeficientes', 'Color', 'w');
    hold on;
    for k = 1:numel(R)
        plot(R(k).n, 10*log10(max(R(k).werr2, realmin)), 'LineWidth', 1.2);
    end
    hold off; grid on;
    xlabel('muestra n'); ylabel('||p - w||^2 (dB)');
    title(strtrim(['Error de los coeficientes ' titulo]));
    legend(leyendas, 'Location', 'northeast', 'Interpreter', 'tex');
    ylim(limites_dB(10*log10(max(vertcat(R.werr2), realmin))));
    figs{end+1} = f;
    guardar(f, carpeta, [prefijo '_werr']);

    % 3) Coeficientes finales vs. planta (sólo las corridas que no divergieron)
    ok = arrayfun(@(r) ~strcmp(r.fin.estado, 'DIVERGE') && ~isempty(r.w), R);
    if any(ok)
        f = figure('Name', 'P1 - coeficientes finales', 'Color', 'w');
        k = 0:numel(R(1).planta)-1;
        stem(k, R(1).planta, 'k', 'filled', 'LineWidth', 1.5, 'DisplayName', 'planta p_k');
        hold on;
        idx = find(ok);
        for j = idx(:)'
            plot(k, R(j).w, 'o--', 'DisplayName', ['w_k ' leyendas{j}]);
        end
        hold off; grid on;
        xlabel('k'); ylabel('coeficiente');
        title(strtrim(['Coeficientes al final ' titulo]));
        legend('Location', 'northeast', 'Interpreter', 'tex');
        figs{end+1} = f;
        guardar(f, carpeta, [prefijo '_coef']);
    end

    % 4) Diferencia de cada coeficiente a lo largo del tiempo
    j = find(ok, 1);
    if ~isempty(j) && ~isempty(R(j).diff)
        f = figure('Name', 'P1 - diferencia de coeficientes', 'Color', 'w');
        subplot(2, 1, 1);
        plot(R(j).n_diff, R(j).diff(:, 1:5), 'LineWidth', 1.1);
        grid on;
        xlabel('muestra n'); ylabel('p_k - w_k');
        legend(arrayfun(@(k) sprintf('k = %d', k), 0:4, 'UniformOutput', false), ...
            'Location', 'northeast');
        title(['Diferencia planta - filtro, ' leyendas{j}], 'Interpreter', 'tex');
        subplot(2, 1, 2);
        imagesc(R(j).n_diff, 0:size(R(j).diff, 2)-1, abs(R(j).diff'));
        axis xy; colorbar;
        xlabel('muestra n'); ylabel('coeficiente k');
        title('|p_k - w_k| (cada 100 muestras)');
        figs{end+1} = f;
        guardar(f, carpeta, [prefijo '_diff']);
    end

    % Tabla resumen en la consola
    fprintf('\n%-12s %-7s %-9s %-9s %-9s %-12s %-12s %-6s %s\n', 'alg', 'in', 'A', 'mu', ...
        'mu_max', 'tau teo', 'MSE final', 'cyc', 'estado');
    for k = 1:numel(R)
        c = R(k).cfg;
        fprintf('%-12s %-7s %-9.3g %-9.3g %-9.3g %-12.4g %-12.4g %-6g %s\n', c.alg, c.in, c.A, ...
            c.mu, c.mu_max, c.tau, R(k).fin.mse, R(k).fin.cyc, R(k).fin.estado);
    end
end

function s = etiqueta(r)
    s = sprintf('\\mu = %g', r.cfg.mu);
    if strcmp(r.fin.estado, 'DIVERGE')
        s = [s ' (diverge)'];
    end
end

function yl = limites_dB(v)
    v = v(isfinite(v));
    if isempty(v)
        yl = [-1 1];
        return;
    end
    lo = max(min(v), -160);
    hi = min(max(v), 60);
    yl = [floor(lo/10)*10, ceil(hi/10)*10 + 5];
    if yl(1) >= yl(2), yl = [yl(1)-10 yl(2)+10]; end
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
