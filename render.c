#include "render.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <windows.h>

#define PI 3.14159265358979323846f

// Noise floor in dB below peak 0 dB level
#define NOISE_FLOOR_DB -36.0f

// Minimum and maximum allowed dynamic sensitivity multipliers
#define SENS_MIN 0.5f
#define SENS_MAX 30.0f
// Target maximum bar height for Autosens adjustment (85% of MAX_HEIGHT)
#define TARGET_HEIGHT 12.75f

// UTF-8 representations of vertical sub-character block fills
static const char* BLOCKS[] = {
    " ",           // empty
    "\xE2\x96\x81", // 1/8
    "\xE2\x96\x82", // 2/8
    "\xE2\x96\x83", // 3/8
    "\xE2\x96\x84", // 4/8
    "\xE2\x96\x85", // 5/8
    "\xE2\x96\x86", // 6/8
    "\xE2\x96\x87", // 7/8
    "\xE2\x96\x88"  // full 8/8
};

static float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

void set_noise_reduction(RenderState *state, int noise_reduction) {
    state->noise_reduction = clamp_int(noise_reduction, 0, 100);
    float nr = (float)state->noise_reduction / 100.0f;
    
    // Weight for integral filter: 0.15 (nr=0, fast) to 0.90 (nr=100, ultra smooth)
    state->integral_weight = 0.15f + nr * 0.75f;
    
    // Gravity acceleration (height_units / sec^2): 180.0 (nr=0, rapid drop) to 20.0 (nr=100, floaty drop)
    state->gravity_accel = 180.0f - nr * 160.0f;
}

void init_render_state(RenderState *state, int noise_reduction, int debug_mode) {
    memset(state->bars, 0, sizeof(state->bars));
    memset(state->fall_velocity, 0, sizeof(state->fall_velocity));
    state->sens = 1.0f;
    state->debug_mode = debug_mode;
    set_noise_reduction(state, noise_reduction);
    QueryPerformanceCounter(&state->last_counter);
}

// Helper to calculate active console width in columns
static int get_console_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w > 0) return w;
    }
    // Fallback: try opening CONOUT$ directly
    HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
    if (hConOut != INVALID_HANDLE_VALUE) {
        if (GetConsoleScreenBufferInfo(hConOut, &csbi)) {
            int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            CloseHandle(hConOut);
            if (w > 0) return w;
        }
        CloseHandle(hConOut);
    }
    return 80;
}

// Helper to interpolate colors (Neon Blue -> Purple -> Neon Pink)
static void get_row_color(int row, int max_rows, int *r_out, int *g_out, int *b_out) {
    float t = (float)row / (max_rows - 1);
    if (t < 0.5f) {
        // Neon Blue (0, 191, 255) to Purple (138, 43, 226)
        float t2 = t * 2.0f;
        *r_out = (int)(0 * (1.0f - t2) + 138 * t2);
        *g_out = (int)(191 * (1.0f - t2) + 43 * t2);
        *b_out = (int)(255 * (1.0f - t2) + 226 * t2);
    } else {
        // Purple (138, 43, 226) to Neon Pink (255, 20, 147)
        float t2 = (t - 0.5f) * 2.0f;
        *r_out = (int)(138 * (1.0f - t2) + 255 * t2);
        *g_out = (int)(43 * (1.0f - t2) + 20 * t2);
        *b_out = (int)(226 * (1.0f - t2) + 147 * t2);
    }
}

void render_frame(const Complex *fft_result, int fft_size, RenderState *state, const char *title, const char *artist) {
    // 1. Precise dt timing calculation via QueryPerformanceCounter
    LARGE_INTEGER count, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    
    float dt = (float)(count.QuadPart - state->last_counter.QuadPart) / (float)freq.QuadPart;
    state->last_counter = count;
    
    // Clamp dt to reasonable bounds (1ms to 100ms) to handle pauses/resizing gracefully
    dt = clampf(dt, 0.001f, 0.1f);
    float fps = 1.0f / dt;
    
    // 2. Group positive frequency bins logarithmically (20 Hz - 18000 Hz)
    int num_bins = fft_size / 2;
    float sample_rate = 44100.0f;
    float delta_f = sample_rate / (float)fft_size;
    float norm_factor = 32768.0f * (fft_size / 2.0f);
    
    float bin_mags[NUM_BARS] = {0};
    float frame_energy = 0.0f;
    
    for (int i = 0; i < NUM_BARS; i++) {
        float f_start = 20.0f * powf(18000.0f / 20.0f, (float)i / NUM_BARS);
        float f_end = 20.0f * powf(18000.0f / 20.0f, (float)(i + 1) / NUM_BARS);
        
        int start_bin = (int)(f_start / delta_f);
        int end_bin = (int)ceilf(f_end / delta_f);
        if (start_bin < 1) start_bin = 1;
        if (end_bin <= start_bin) end_bin = start_bin + 1;
        if (end_bin > num_bins) end_bin = num_bins;
        
        float sum = 0.0f;
        for (int b = start_bin; b < end_bin; b++) {
            float mag = sqrtf(fft_result[b].r * fft_result[b].r + fft_result[b].i * fft_result[b].i);
            sum += mag;
        }
        float avg = sum / (float)(end_bin - start_bin);
        bin_mags[i] = avg;
        frame_energy += avg;
    }
    frame_energy /= NUM_BARS;
    
    // 3. Convert magnitude to dB & raw height with safe log10 guard
    float raw_heights[NUM_BARS] = {0};
    float bar_max = 0.0f;
    
    for (int i = 0; i < NUM_BARS; i++) {
        float norm_mag = bin_mags[i] / norm_factor;
        float val = 0.0f;
        
        if (norm_mag > 1e-7f) {
            float scaled_mag = norm_mag * state->sens;
            if (scaled_mag > 1e-7f) {
                float db = 20.0f * log10f(scaled_mag);
                if (db > NOISE_FLOOR_DB) {
                    val = ((db - NOISE_FLOOR_DB) / (-NOISE_FLOOR_DB)) * MAX_HEIGHT;
                }
            }
        }
        val = clampf(val, 0.0f, (float)MAX_HEIGHT);
        raw_heights[i] = val;
        if (val > bar_max) bar_max = val;
    }
    
    // 4. Autosens (Dynamic Sensitivity Adjustment with Hysteresis & Silence Gate)
    if (frame_energy >= 1e-5f * norm_factor) {
        if (bar_max >= (float)MAX_HEIGHT) {
            // Rapid response down when hitting ceiling (prevents clipping)
            state->sens *= 0.98f;
        } else if (bar_max < TARGET_HEIGHT && bar_max > 0.5f) {
            // Slow response up when audio is quiet (prevents pumping)
            state->sens *= 1.002f;
        }
        state->sens = clampf(state->sens, SENS_MIN, SENS_MAX);
    }
    
    // 5. Separate Integral Filter (Rising) vs Gravity Fall-off (Falling) normalized by dt
    for (int i = 0; i < NUM_BARS; i++) {
        float target = raw_heights[i];
        
        if (target > state->bars[i]) {
            // RISING: Temporal Integral Filter Smoothing
            float alpha = 1.0f - powf(state->integral_weight, dt * 60.0f);
            state->bars[i] += (target - state->bars[i]) * alpha;
            state->fall_velocity[i] = 0.0f; // Reset fall acceleration
        } else {
            // FALLING: Gravity Physical Acceleration (* dt)
            state->fall_velocity[i] += state->gravity_accel * dt;
            state->bars[i] -= state->fall_velocity[i] * dt;
            
            // Floor clamp to target height to prevent undershoot
            if (state->bars[i] < target) {
                state->bars[i] = target;
                state->fall_velocity[i] = 0.0f;
            }
            if (state->bars[i] < 0.0f) {
                state->bars[i] = 0.0f;
            }
        }
    }
    
    // 6. Memory Buffer Render (ANSI 24-bit Truecolor UTF-8 blocks with dynamic centering)
    char buffer[32768];
    int offset = 0;
    
    int console_width = get_console_width();
    int total_width = NUM_BARS * 4;
    int horizontal_offset = (console_width - total_width) / 2;
    if (horizontal_offset < 0) horizontal_offset = 0;
    
    char left_pad[256] = {0};
    for (int p = 0; p < horizontal_offset && p < 255; p++) {
        left_pad[p] = ' ';
    }
    
    // Reset cursor to top-left
    offset += sprintf(buffer + offset, "\033[H");
    
    // Render Debug Overlay if enabled
    if (state->debug_mode) {
        offset += sprintf(buffer + offset, "%s\033[93m[DEBUG] sens: %.2f | nr: %d | bar_max: %.2f | dt: %.4fs (%.1f FPS)\033[0m\033[K\n",
                          left_pad, state->sens, state->noise_reduction, bar_max, dt, fps);
    }
    
    // Render vertical bars top-to-bottom
    for (int row = MAX_HEIGHT - 1; row >= 0; row--) {
        int r_val, g_val, b_val;
        get_row_color(row, MAX_HEIGHT, &r_val, &g_val, &b_val);
        
        offset += sprintf(buffer + offset, "%s", left_pad);
        offset += sprintf(buffer + offset, "\033[38;2;%d;%d;%dm", r_val, g_val, b_val);
        
        for (int col = 0; col < NUM_BARS; col++) {
            float h = state->bars[col];
            if (h >= row + 1) {
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[8]);
            } else if (h < row) {
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[0]);
            } else {
                float fraction = h - row;
                int idx = (int)(fraction * 8.0f + 0.5f);
                if (idx < 0) idx = 0;
                if (idx > 8) idx = 8;
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[idx]);
            }
        }
        offset += sprintf(buffer + offset, "\033[0m\033[K\n");
    }
    
    // Render metadata line centered
    offset += sprintf(buffer + offset, "\n");
    
    char info_line[256];
    if (strlen(title) > 0) {
        sprintf(info_line, "🎵 %s - %s", title, artist);
    } else {
        sprintf(info_line, "🎵 Silencio o Desconocido");
    }
    
    int len = (int)strlen(info_line);
    int padding = (total_width - len) / 2;
    if (padding < 0) padding = 0;
    
    offset += sprintf(buffer + offset, "%s", left_pad);
    for (int p = 0; p < padding; p++) {
        offset += sprintf(buffer + offset, " ");
    }
    offset += sprintf(buffer + offset, "\033[38;2;255;105;180m%s\033[0m\033[K\n", info_line);
    
    // Render footer centered
    char footer[] = "Presiona Ctrl+C para salir.";
    int footer_len = (int)strlen(footer);
    int footer_padding = (total_width - footer_len) / 2;
    if (footer_padding < 0) footer_padding = 0;
    
    offset += sprintf(buffer + offset, "%s", left_pad);
    for (int p = 0; p < footer_padding; p++) {
        offset += sprintf(buffer + offset, " ");
    }
    offset += sprintf(buffer + offset, "\033[90m%s\033[0m\033[K\n", footer);
    
    printf("%s", buffer);
    fflush(stdout);
}


