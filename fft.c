#include "fft.h"
#include <math.h>

#define PI 3.14159265358979323846f

unsigned int reverseBits(unsigned int num, unsigned int log2n) {
    unsigned int reverse_num = 0;
    for (unsigned int i = 0; i < log2n; i++) {
        if ((num & (1 << i))) {
            reverse_num |= 1 << ((log2n - 1) - i);
        }
    }
    return reverse_num;
}

void fft(Complex *X, int N) {
    int log2n = 0;
    while ((1 << log2n) < N) {
        log2n++;
    }
    
    for (int i = 0; i < N; i++) {
        unsigned int rev = reverseBits(i, log2n);
        if (i < rev) {
            Complex temp = X[i];
            X[i] = X[rev];
            X[rev] = temp;
        }
    }
    
    for (int len = 2; len <= N; len <<= 1) {
        float angle = -2.0f * PI / len;
        Complex wlen = { cosf(angle), sinf(angle) };
        for (int i = 0; i < N; i += len) {
            Complex w = { 1.0f, 0.0f };
            for (int j = 0; j < len / 2; j++) {
                Complex u = X[i + j];
                
                Complex v;
                v.r = X[i + j + len / 2].r * w.r - X[i + j + len / 2].i * w.i;
                v.i = X[i + j + len / 2].r * w.i + X[i + j + len / 2].i * w.r;
                
                X[i + j].r = u.r + v.r;
                X[i + j].i = u.i + v.i;
                X[i + j + len / 2].r = u.r - v.r;
                X[i + j + len / 2].i = u.i - v.i;
                
                Complex next_w;
                next_w.r = w.r * wlen.r - w.i * wlen.i;
                next_w.i = w.r * wlen.i + w.i * wlen.r;
                w = next_w;
            }
        }
    }
}
