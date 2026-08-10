#ifndef RENDER_H
#define RENDER_H

#include "fft.h"
#include <windows.h>

#define NUM_BARS 20
#define MAX_HEIGHT 15

typedef struct {
    float bars[NUM_BARS];
    float fall_velocity[NUM_BARS];
    float sens;
    int noise_reduction;
    float integral_weight;
    float gravity_accel;
    int debug_mode;
    LARGE_INTEGER last_counter;
} RenderState;

// Initialize render state with noise reduction (0-100) and debug mode flag
void init_render_state(RenderState *state, int noise_reduction, int debug_mode);

// Update noise reduction parameter dynamically
void set_noise_reduction(RenderState *state, int noise_reduction);

// Process FFT output, calculate logarithmic bands, apply autosens/smoothing/gravity, and render the frame
void render_frame(const Complex *fft_result, int fft_size, RenderState *state, const char *title, const char *artist);

#endif // RENDER_H

