#ifndef WINDOW_H
#define WINDOW_H

#include "fft.h"

// Applies Hann window to input data and stores in Complex array
void apply_hann_window(const short *input, Complex *output, int N);

#endif // WINDOW_H
