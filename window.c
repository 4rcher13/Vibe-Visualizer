#include "window.h"
#include <math.h>

#define PI 3.14159265358979323846f

void apply_hann_window(const short *input, Complex *output, int N) {
    for (int i = 0; i < N; i++) {
        float window = 0.5f * (1.0f - cosf(2.0f * PI * i / (N - 1)));
        output[i].r = (float)input[i] * window;
        output[i].i = 0.0f;
    }
}
