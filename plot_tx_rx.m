figure(1);
x = readcplx16('tx_data.dat');
nx = 1:length(x);
plot(nx, real(x),'.', nx, imag(x),'.');

figure(2);
y = readcplx16('rx_data.dat');
ny = 1:length(y);
plot(ny, real(y),'.', ny, imag(y),'.');

istart = min(find(y ~= 0))

nn = istart:(istart - 1 + length(x));
xx = complex(imag(y(nn)), real(y(nn)));
er = x - xx;
norm(er, 1)

figure(3); 
plot(1:length(x), real(er), '.', 1:length(x), imag(er), '.'); 
grid on;