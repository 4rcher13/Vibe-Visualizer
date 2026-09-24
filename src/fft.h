#ifndef FFT_H
#define FFT_H

typedef struct {
    float r; // Real part
    float i; // Imaginary part
} Complex;

// Cooley-Tukey FFT algorithm
// N must be a power of 2
void fft(Complex *X, int N);

#endif // FFT_H
