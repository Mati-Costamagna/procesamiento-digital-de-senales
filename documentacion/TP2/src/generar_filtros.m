%% generar_filtros.m
% Convierte los filtros FIR disenados en fdatool / filterDesigner en el
% archivo FiltrosTP2.h que usa TP2.c (cuantizados a Q15, orden CMSIS).
%
% PASOS PREVIOS, por cada filtro disenado en fdatool:
%   File -> Export...  ->  Export To: Workspace
%                          Export As: Coefficients
%                          Numerator: <nombre>        (ver nombres abajo)
%
% Nombres de las variables (tipo_fs):
%   PB_8k   PA_8k   PBanda_8k   EB_8k
%   PB_16k  PA_16k  PBanda_16k  EB_16k   ... (y asi con 22k, 44k, 48k)
%
% IMPORTANTE: NO ejecutes "clear" antes de este script, porque borraria los
% filtros exportados. Corre este script desde el mismo workspace.
%
% Si usas solo 8 kHz: exporta los 4 filtros de 8k y deja FS = 8000.
% Si usas mas fs, tienen que ser las CINCO, en este orden (TP2.c indexa
% las filas con la fs de muestreo): 8k, 16k, 22k, 44k, 48k.

%% ---------------- Configuracion ----------------
FS        = [8000 16000 22000 44000 48000];                  % o [8000 16000 22000 44000 48000]
FS_NOMBRE = {'8k','16k','22k','44k','48k'};                % o {'8k','16k','22k','44k','48k'}
TIPOS     = {'PB', 'PA', 'PBanda', 'EB'};

F_CPU          = 150e6;            % reloj del Cortex-M33
CICLOS_POR_TAP = 5;                % estimado para arm_fir_q15 con blockSize = 1

%% ---------------- Lectura y cuantizacion ----------------
nFs = numel(FS);  nT = numel(TIPOS);
coef = cell(nFs, nT);
taps = zeros(nFs, nT);

for i = 1:nFs
  for k = 1:nT
    nom = sprintf('%s_%s', TIPOS{k}, FS_NOMBRE{i});
    if ~exist(nom, 'var')
      error('Falta la variable "%s" en el workspace. Exportala desde fdatool (File > Export, Coefficients).', nom);
    end
    b = double(eval(nom));         % double() tambien convierte objetos fi
    b = b(:).';                    % fila

    % Si algun coeficiente llega a 1.0 no entra en Q15: escalo apenas
    % (diferencia < 0.01 dB) para que no sature.
    if max(abs(b)) >= 32767/32768
      b = b * (32767/32768) / max(abs(b));
    end

    q = round(b * 32768);          % Q15
    q = fliplr(q);                 % CMSIS espera b[N-1] ... b[0]
    if mod(numel(q), 2) == 1       % arm_fir_init_q15: numTaps par y >= 4
      q(end + 1) = 0;
    end
    while numel(q) < 4, q(end + 1) = 0; end

    coef{i, k} = q;
    taps(i, k) = numel(q);
  end
end

%% ---------------- Resumen ----------------
fprintf('\n%-6s %-7s %7s %12s\n', 'fs', 'Filtro', 'Taps', 'CPU estimada');
for i = 1:nFs
  for k = 1:nT
    uso = 100 * taps(i, k) * CICLOS_POR_TAP * FS(i) / F_CPU;
    if uso > 100, aviso = '  <- NO entra en tiempo real'; else, aviso = ''; end
    fprintf('%-6s %-7s %7d %10.0f %%%s\n', FS_NOMBRE{i}, TIPOS{k}, taps(i, k), uso, aviso);
  end
end
fprintf('Coeficientes totales: %d (%.1f KB de flash)\n', sum(taps(:)), 2*sum(taps(:))/1024);

%% ---------------- Escritura de FiltrosTP2.h ----------------
fid = fopen('filtros.h', 'w');
fprintf(fid, '/*\n * FiltrosTP2.h - generado por generar_header_desde_fdatool.m (%s)\n', datestr(now));
fprintf(fid, ' * Filtros FIR disenados en fdatool, coeficientes Q15 en orden invertido (CMSIS).\n');
fprintf(fid, ' * Incluir SOLO desde un .c (los arreglos son static const).\n */\n\n');
fprintf(fid, '#ifndef FILTROSTP2_H_\n#define FILTROSTP2_H_\n\n#include "arm_math.h"\n\n');
fprintf(fid, '#define NUM_FS     %d\n#define NUM_TIPOS  %d\n#define MAX_TAPS   %d\n\n', nFs, nT, max(taps(:)));
fprintf(fid, 'enum { PB = 0, PA, PBANDA, EB };\n');
fprintf(fid, 'enum { K8 = 0, K16, K22, K44, K48 };\n\n');

s = sprintf('%d, ', FS);  s = s(1:end-2);
fprintf(fid, 'static const uint32_t fs_hz[NUM_FS] = {%s};\n', s);
fprintf(fid, 'static const char * const nombre_tipo[NUM_TIPOS] = {"%s"};\n\n', strjoin(TIPOS, '", "'));

for i = 1:nFs
  for k = 1:nT
    nom = sprintf('%s_%s', TIPOS{k}, FS_NOMBRE{i});
    fprintf(fid, '#define NUM_TAPS_%s %d\n', upper(nom), taps(i, k));
    fprintf(fid, 'static const q15_t %s[NUM_TAPS_%s] = {\n', nom, upper(nom));
    q = coef{i, k};
    for j = 1:12:numel(q)
      fila = q(j:min(j + 11, numel(q)));
      s = sprintf('%d, ', fila);  s = s(1:end-2);
      fprintf(fid, '  %s,\n', s);
    end
    fprintf(fid, '};\n\n');
  end
end

fprintf(fid, 'static const q15_t * const coef_tabla[NUM_FS][NUM_TIPOS] = {\n');
for i = 1:nFs
  nombres = cell(1, nT);
  for k = 1:nT, nombres{k} = sprintf('%s_%s', TIPOS{k}, FS_NOMBRE{i}); end
  fprintf(fid, '  {%s},\n', strjoin(nombres, ', '));
end
fprintf(fid, '};\n\nstatic const uint16_t taps_tabla[NUM_FS][NUM_TIPOS] = {\n');
for i = 1:nFs
  s = sprintf('%d, ', taps(i, :));  s = s(1:end-2);
  fprintf(fid, '  {%s},\n', s);
end
fprintf(fid, '};\n\n#endif /* FILTROSTP2_H_ */\n');
fclose(fid);
fprintf('\nListo: FiltrosTP2.h escrito en %s\n', pwd);