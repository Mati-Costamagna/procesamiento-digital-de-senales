% Referencia de escritorio: no sustituye el calculo en la placa.
clear; close all;
Fs = 8000;
N = 512;                 % Probar 512, 1024 y 2048
f0 = 1000;               % Probar luego 1005: leakage
n = (0:N-1).';
x = 0.5*sin(2*pi*f0*n/Fs);
q = int16(max(-32768,min(32767,round(32768*x))));
x = double(q)/32768;
X = fft(x);
A = abs(X(1:N/2+1))/N;
A(2:end-1) = 2*A(2:end-1);
f = (0:N/2).'*Fs/N;
tiledlayout(2,1);
nexttile; plot(n/Fs,x); grid on; xlabel('Tiempo (s)'); ylabel('Amplitud');
nexttile; stem(f,A,'.'); grid on; xlabel('Frecuencia (Hz)'); ylabel('Amplitud de pico');
fprintf('Fs=%g Hz, N=%d, duracion=%g ms, separacion=%g Hz\n',Fs,N,1000*N/Fs,Fs/N);
[p,k] = max(A); fprintf('Pico: %g Hz, amplitud %.6f\n',f(k),p);