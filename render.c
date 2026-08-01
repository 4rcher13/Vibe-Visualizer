#include "render.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f

// UTF-8 representations of vertical sub-character block fills
static const char* BLOCKS[] = {
    " ",       // empty
    "\u2581",  // 1/8
    "\u2582",  // 2/8
    "\u2583",  // 3/8
    "\u2584",  // 4/8
    "\u2585",  // 5/8
    "\u2586",  // 6/8
    "\u2587",  // 7/8
    "\u2588"   // full 8/8
};

void init_render_state(RenderState *state) {
    memset(state->prev_heights, 0, sizeof(state->prev_heights));
}

// Helper to interpolate colors (Neon Blue -> Purple -> Neon Pink)
void get_row_color(int row, int max_rows, int *r_out, int *g_out, int *b_out) {
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
    float raw_heights[NUM_BARS] = {0};
    
    // Group 512 positive frequency bins logarithmically
    int num_bins = fft_size / 2;
    int low_cutoff = 2;     // Ignore DC & sub-bass noise
    int high_cutoff = 180;  // High frequency cutoff (standard active music range)
    
    for (int i = 0; i < NUM_BARS; i++) {
        // Logarithmic bounds calculation
        float f_start = low_cutoff * powf((float)high_cutoff / low_cutoff, (float)i / NUM_BARS);
        float f_end = low_cutoff * powf((float)high_cutoff / low_cutoff, (float)(i + 1) / NUM_BARS);
        
        int start_bin = (int)f_start;
        int end_bin = (int)f_end;
        if (end_bin <= start_bin) end_bin = start_bin + 1;
        if (end_bin > num_bins) end_bin = num_bins;
        
        float sum = 0.0f;
        for (int b = start_bin; b < end_bin; b++) {
            float mag = sqrtf(fft_result[b].r * fft_result[b].r + fft_result[b].i * fft_result[b].i);
            sum += mag;
        }
        float avg = sum / (end_bin - start_bin);
        
        // Scale and apply a soft logarithmic compression to the amplitude
        float val = 0.0f;
        if (avg > 0.0f) {
            val = log10f(1.0f + avg * 0.02f) * 12.0f; // Scale it to max height
        }
        
        if (val > MAX_HEIGHT) val = (float)MAX_HEIGHT;
        if (val < 0.0f) val = 0.0f;
        
        raw_heights[i] = val;
    }
    
    // Apply smoothing (rise damping and fall decay)
    float current_heights[NUM_BARS];
    float rise_factor = 0.45f;
    float fall_factor = 0.85f;
    
    for (int i = 0; i < NUM_BARS; i++) {
        float target = raw_heights[i];
        float prev = state->prev_heights[i];
        if (target > prev) {
            current_heights[i] = prev * (1.0f - rise_factor) + target * rise_factor;
        } else {
            current_heights[i] = prev * fall_factor + target * (1.0f - fall_factor);
        }
        state->prev_heights[i] = current_heights[i];
    }
    
    // Draw the frame to a buffer in memory to avoid screen tearing/flickering
    char buffer[8192];
    int offset = 0;
    
    // Cursor to top-left (ANSI escape sequence)
    offset += sprintf(buffer + offset, "\033[H");
    
    // Draw vertical bars from top to bottom
    for (int row = MAX_HEIGHT - 1; row >= 0; row--) {
        // Calculate gradient color for this row
        int r_val, g_val, b_val;
        get_row_color(row, MAX_HEIGHT, &r_val, &g_val, &b_val);
        
        // Set row color (ANSI 24-bit Truecolor foreground)
        offset += sprintf(buffer + offset, "\033[38;2;%d;%d;%dm", r_val, g_val, b_val);
        
        for (int col = 0; col < NUM_BARS; col++) {
            float h = current_heights[col];
            if (h >= row + 1) {
                // Fully filled block
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[8]);
            } else if (h < row) {
                // Empty block
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[0]);
            } else {
                // Partially filled block
                float fraction = h - row;
                int idx = (int)(fraction * 8.0f + 0.5f);
                if (idx < 0) idx = 0;
                if (idx > 8) idx = 8;
                offset += sprintf(buffer + offset, " %s  ", BLOCKS[idx]);
            }
        }
        offset += sprintf(buffer + offset, "\033[0m\n"); // Reset color and new line
    }
    
    // Print metadata
    offset += sprintf(buffer + offset, "\n");
    
    char info_line[256];
    if (strlen(title) > 0) {
        sprintf(info_line, "🎵 %s - %s", title, artist);
    } else {
        sprintf(info_line, "🎵 Silencio o Desconocido");
    }
    
    int total_width = NUM_BARS * 4;
    int len = (int)strlen(info_line);
    int padding = (total_width - len) / 2;
    if (padding < 0) padding = 0;
    
    for (int p = 0; p < padding; p++) {
        offset += sprintf(buffer + offset, " ");
    }
    
    // Print in Neon Pink/Orange color
    offset += sprintf(buffer + offset, "\033[38;2;255;105;180m%s\033[0m\n", info_line);
    
    // Print footer centered
    char footer[] = "Presiona Ctrl+C para salir.";
    int footer_len = (int)strlen(footer);
    int footer_padding = (total_width - footer_len) / 2;
    if (footer_padding < 0) footer_padding = 0;
    for (int p = 0; p < footer_padding; p++) {
        offset += sprintf(buffer + offset, " ");
    }
    offset += sprintf(buffer + offset, "\033[90m%s\033[0m\n", footer);
    
    printf("%s", buffer);
    fflush(stdout);
}
