#ifndef RENDER_H
#define RENDER_H

#include "fft.h"

#define NUM_BARS 20
#define MAX_HEIGHT 15

typedef struct {
    float prev_heights[NUM_BARS];
    float peak_mag;
} RenderState;

// Initialize render state
void init_render_state(RenderState *state);

// Process FFT output, calculate logarithmic bands, apply decay/smoothing, and render the frame
void render_frame(const Complex *fft_result, int fft_size, RenderState *state, const char *title, const char *artist);

#endif // RENDER_H
