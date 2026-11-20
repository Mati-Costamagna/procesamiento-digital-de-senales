function d = diagnosticar_tp3(puerto, baud)
% Captura una trama real de la placa y muestra sus valores sin graficar.
% Cerrar recibir_tp3 antes de llamar: d = diagnosticar_tp3("COM8",115200)
if nargin < 2, baud = 115200; end
s = serialport(puerto, baud, "Timeout", 10);
configureTerminator(s, "CR/LF");
limpiezaPuerto = onCleanup(@() delete(s));
flush(s, "input");
inicio = tic;
meta = [];
while toc(inicio) < 15
    cabecera = strtrim(readline(s));
    if startsWith(cabecera, "TP3,")
        meta = sscanf(char(extractAfter(cabecera,"TP3,")), '%f,%f,%f,%f');
        if numel(meta) == 4, break; end
    end
end
if numel(meta) ~= 4
    error('TP3:SinCabecera','No se recibio una cabecera TP3 valida.');
end
d.trama = meta(1);
d.modo = meta(2);
d.N = meta(3);
d.Fs = meta(4)/1000;
if ~ismember(d.N,[512 1024 2048]) || ~ismember(d.modo,[0 1]) || d.Fs <= 0
    error('TP3:Cabecera','Cabecera fuera del formato esperado.');
end
d.hexReal = strings(d.N,1);
d.hexImag = strings(d.N,1);
d.X = complex(zeros(d.N,1));
for k = 0:d.N-1
    fila = char(strtrim(readline(s)));
    tok = regexp(fila,'^(\d+),([0-9A-F]{8}),([0-9A-F]{8})$','tokens','once');
    if isempty(tok) || str2double(tok{1}) ~= k
        error('TP3:Fila','Fila invalida o desordenada en k=%d: %s',k,fila);
    end
    d.hexReal(k+1) = string(tok{2});
    d.hexImag(k+1) = string(tok{3});
    re = double(typecast(uint32(hex2dec(tok{2})),'single'));
    im = double(typecast(uint32(hex2dec(tok{3})),'single'));
    d.X(k+1) = complex(re,im);
end
if strtrim(readline(s)) ~= "END"
    error('TP3:Final','Falta END al terminar la trama.');
end
clear limpiezaPuerto;

carpeta = fileparts(mfilename('fullpath'));
base = fullfile(carpeta,sprintf('diagnostico_tp3_modo%d',d.modo));
save([base '.mat'],'d');
[fid,mensaje] = fopen([base '.txt'],'w');
if fid < 0, error('TP3:Archivo','%s',mensaje); end
limpiezaArchivo = onCleanup(@() fclose(fid));
for destino = [1 fid]
    fprintf(destino,'Trama=%d | modo=%d | N=%d | Fs=%.6f Hz\n',d.trama,d.modo,d.N,d.Fs);
    fprintf(destino,'Real: min=%.9g max=%.9g media=%.9g std=%.9g\n', ...
        min(real(d.X)),max(real(d.X)),mean(real(d.X)),std(real(d.X)));
    fprintf(destino,'Imag: min=%.9g max=%.9g\n',min(imag(d.X)),max(imag(d.X)));
    fprintf(destino,'Maximo absoluto=%.9g | Valores no nulos=%d/%d | No finitos=%d\n', ...
        max(abs(d.X)),nnz(d.X),d.N,sum(~isfinite(d.X)));
    fprintf(destino,'Primeros 12 datos (muestras en bypass; bins en FFT):\n');
    fprintf(destino,'k    HEX REAL    HEX IMAG       REAL              IMAG\n');
    for k = 1:min(12,d.N)
        fprintf(destino,'%4d %s %s % .9e % .9e\n',k-1, ...
            char(d.hexReal(k)),char(d.hexImag(k)),real(d.X(k)),imag(d.X(k)));
    end
    if all(d.X == 0)
        fprintf(destino,'Todos los valores decodificados de esta trama son cero.\n');
    elseif d.modo == 0
        fprintf(destino,'Excursion pico a pico normalizada=%.9g\n',max(real(d.X))-min(real(d.X)));
    else
        amplitud = abs(d.X(1:d.N/2+1))/d.N;
        amplitud(2:end-1) = 2*amplitud(2:end-1);
        [pico,idx] = max(amplitud(2:end));
        fprintf(destino,'DC=%.9g | Mayor pico sin DC: f=%.6f Hz, A=%.9g\n', ...
            amplitud(1),idx*d.Fs/d.N,pico);
    end
end
fprintf('Diagnostico guardado en:\n%s.txt\n',base);
end
