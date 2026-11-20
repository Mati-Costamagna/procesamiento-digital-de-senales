function recibir_tp3(puerto, baud)
% Ejemplo: recibir_tp3("COM5",115200). Cerrar otras apps que usen ese COM.
% Requiere tp3_fft.c y una UART real con el mismo baud.
if nargin < 2, baud = 115200; end
s = serialport(puerto,baud,"Timeout",3);
configureTerminator(s,"CR/LF");
cleanup = onCleanup(@() delete(s)); %#ok<NASGU>
fig = figure('Name','TP3: FFT calculada en la placa');
ax = axes(fig);
while isgraphics(fig)
    drawnow;
    if ~isgraphics(fig), break; end
    try
        line = strtrim(readline(s));
        if ~startsWith(line,"TP3,"), continue; end
        meta = sscanf(char(extractAfter(line,"TP3,")),'%f,%f,%f,%f');
        if numel(meta) ~= 4, continue; end
        seq = meta(1); modo = meta(2); N = meta(3); Fs = meta(4)/1000;
        if ~ismember(N,[512 1024 2048]) || ~ismember(modo,[0 1]) || Fs<=0
            continue;
        end
        X = complex(zeros(N,1));
        for k = 0:N-1
            row = char(strtrim(readline(s)));
            tok = regexp(row,'^(\d+),([0-9A-F]{8}),([0-9A-F]{8})$','tokens','once');
            if isempty(tok) || str2double(tok{1}) ~= k
                error('TP3:Frame','Trama incompleta o desordenada.');
            end
            re = double(typecast(uint32(hex2dec(tok{2})),'single'));
            im = double(typecast(uint32(hex2dec(tok{3})),'single'));
            X(k+1) = complex(re,im);
        end
        if strtrim(readline(s)) ~= "END"
            error('TP3:Frame','Falta el final de trama.');
        end
        if ~isgraphics(fig), break; end
        if modo == 1
            A = abs(X(1:N/2+1))/N;
            A(2:end-1) = 2*A(2:end-1);
            plot(ax,(0:N/2)*Fs/N,A);
            xlabel(ax,'Frecuencia (Hz)'); ylabel(ax,'Amplitud de pico normalizada');
            xlim(ax,[0 Fs/2]);
        else
            plot(ax,(0:N-1)/Fs,real(X));
            xlabel(ax,'Tiempo (s)'); ylabel(ax,'Muestra normalizada (bypass)');
            xlim(ax,[0 (N-1)/Fs]);
        end
        grid(ax,'on');
        title(ax,sprintf('Trama %d | modo %d | N=%d | Fs=%.3f Hz',seq,modo,N,Fs));
        drawnow;
    catch err
        if ~isgraphics(fig), break; end
        warning('TP3:Recepcion','%s',err.message);
  end
end
end
