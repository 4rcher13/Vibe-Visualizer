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
    state->last_width = 0;
    state->last_height = 0;
    set_noise_reduction(state, noise_reduction);
    QueryPerformanceCounter(&state->last_counter);
}

// Helper to calculate active console width and height
static void get_console_size(int *width_out, int *height_out) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (w > 0 && h > 0) {
            *width_out = w;
            *height_out = h;
            return;
        }
    }
    // Fallback: try opening CONOUT$ directly
    HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
    if (hConOut != INVALID_HANDLE_VALUE) {
        if (GetConsoleScreenBufferInfo(hConOut, &csbi)) {
            int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            CloseHandle(hConOut);
            if (w > 0 && h > 0) {
                *width_out = w;
                *height_out = h;
                return;
            }
        }
        CloseHandle(hConOut);
    }
    *width_out = 80;
    *height_out = 24;
}

// Calculate visual display width of UTF-8 strings in terminal cells
static int utf8_display_width(const char *s) {
    int width = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            width += 1;
            s += 1;
        } else if ((c & 0xE0) == 0xC0) {
            width += 1;
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {
            width += 1;
            s += 3;
        } else if ((c & 0xF8) == 0xF0) {
            width += 2;
            s += 4;
        } else {
            s++;
        }
    }
    return width;
}

// Truncate UTF-8 string to a maximum display cell width, appending "..." if truncated
static void truncate_to_display_width(const char *src, char *dest, int max_dest_bytes, int max_display_width) {
    int cur_width = 0;
    int s_idx = 0;
    int d_idx = 0;
    int total_width = utf8_display_width(src);
    
    if (total_width <= max_display_width) {
        strncpy(dest, src, max_dest_bytes - 1);
        dest[max_dest_bytes - 1] = '\0';
        return;
    }
    
    int target_width = max_display_width - 3;
    if (target_width < 1) target_width = 1;
    
    while (src[s_idx] && d_idx < max_dest_bytes - 5) {
        unsigned char c = (unsigned char)src[s_idx];
        int char_bytes = 1;
        int char_width = 1;
        
        if (c < 0x80) {
            char_bytes = 1;
            char_width = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_bytes = 2;
            char_width = 1;
        } else if ((c & 0xF0) == 0xE0) {
            char_bytes = 3;
            char_width = 1;
        } else if ((c & 0xF8) == 0xF0) {
            char_bytes = 4;
            char_width = 2;
        }
        
        if (cur_width + char_width > target_width) {
            break;
        }
        
        for (int b = 0; b < char_bytes && src[s_idx + b]; b++) {
            dest[d_idx++] = src[s_idx + b];
        }
        s_idx += char_bytes;
        cur_width += char_width;
    }
    
    dest[d_idx++] = '.';
    dest[d_idx++] = '.';
    dest[d_idx++] = '.';
    dest[d_idx] = '\0';
}

// Helper to interpolate colors (Neon Blue -> Purple -> Neon Pink)
static void get_row_color(int row, int max_rows, int *r_out, int *g_out, int *b_out) {
    if (max_rows <= 1) {
        *r_out = 138; *g_out = 43; *b_out = 226;
        return;
    }
    float t = (float)row / (float)(max_rows - 1);
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
    
    // Clamp dt to reasonable bounds (1ms to 100ms)
    dt = clampf(dt, 0.001f, 0.1f);
    float fps = 1.0f / dt;

    // Detect console size
    int console_width, console_height;
    get_console_size(&console_width, &console_height);

    // Detect window resize: immediately clear screen to eliminate ghosting & buffer reflow artifacts
    if (console_width != state->last_width || console_height != state->last_height) {
        state->last_width = console_width;
        state->last_height = console_height;
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            DWORD written;
            DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
            COORD origin = {0, 0};
            FillConsoleOutputCharacterA(hConsole, ' ', cells, origin, &written);
            FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cells, origin, &written);
            SetConsoleCursorPosition(hConsole, origin);
        }
        printf("\033[2J\033[3J\033[H");
        fflush(stdout);
    }

    // Dynamic height calculation: guarantee total rendered lines never exceed console_height
    int non_bar_lines = (state->debug_mode ? 1 : 0) + (console_height >= 16 ? 3 : 2) + 1;
    int active_height = console_height - non_bar_lines;
    if (active_height > MAX_HEIGHT) active_height = MAX_HEIGHT;
    if (active_height < 3) active_height = 3;
    
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
                    val = ((db - NOISE_FLOOR_DB) / (-NOISE_FLOOR_DB)) * (float)active_height;
                }
            }
        }
        val = clampf(val, 0.0f, (float)active_height);
        raw_heights[i] = val;
        if (val > bar_max) bar_max = val;
    }
    
    // 4. Autosens (Dynamic Sensitivity Adjustment with Hysteresis & Silence Gate)
    float target_height = (float)active_height * 0.85f;
    if (frame_energy >= 1e-5f * norm_factor) {
        if (bar_max >= (float)active_height) {
            // Rapid response down when hitting ceiling
            state->sens *= 0.98f;
        } else if (bar_max < target_height && bar_max > 0.5f) {
            // Slow response up when audio is quiet
            state->sens *= 1.002f;
        }
        state->sens = clampf(state->sens, SENS_MIN, SENS_MAX);
    }
    
    // 5. Separate Integral Filter (Rising) vs Gravity Fall-off (Falling)
    float gravity_scaled = state->gravity_accel * ((float)active_height / (float)MAX_HEIGHT);
    for (int i = 0; i < NUM_BARS; i++) {
        float target = raw_heights[i];
        
        if (target > state->bars[i]) {
            // RISING: Temporal Integral Filter Smoothing
            float alpha = 1.0f - powf(state->integral_weight, dt * 60.0f);
            state->bars[i] += (target - state->bars[i]) * alpha;
            state->fall_velocity[i] = 0.0f;
        } else {
            // FALLING: Gravity Physical Acceleration (* dt)
            state->fall_velocity[i] += gravity_scaled * dt;
            state->bars[i] -= state->fall_velocity[i] * dt;
            
            if (state->bars[i] < target) {
                state->bars[i] = target;
                state->fall_velocity[i] = 0.0f;
            }
            if (state->bars[i] < 0.0f) {
                state->bars[i] = 0.0f;
            }
        }
    }
    
    // 6. Memory Buffer Render (Responsive Bar Width, Spacing, and Dynamic Centering)
    int usable_width = console_width - 2;
    if (usable_width < 6) usable_width = 6;

    int num_bars = NUM_BARS;
    int bar_block_width = 1;
    int bar_lead_spaces = 0;
    int bar_trail_spaces = 1;

    // Adapt bar layout so it never wraps regardless of window width
    if (usable_width >= 140) {
        bar_block_width = 2;
        bar_lead_spaces = 0;
        bar_trail_spaces = 4; // 6 cols per bar -> 120 cols
    } else if (usable_width >= 105) {
        bar_block_width = 2;
        bar_lead_spaces = 0;
        bar_trail_spaces = 3; // 5 cols per bar -> 100 cols
    } else if (usable_width >= 84) {
        bar_block_width = 1;
        bar_lead_spaces = 1;
        bar_trail_spaces = 2; // 4 cols per bar (" %s  ") -> 80 cols
    } else if (usable_width >= 64) {
        bar_block_width = 1;
        bar_lead_spaces = 1;
        bar_trail_spaces = 1; // 3 cols per bar (" %s ") -> 60 cols
    } else if (usable_width >= 44) {
        bar_block_width = 1;
        bar_lead_spaces = 0;
        bar_trail_spaces = 1; // 2 cols per bar ("%s ") -> 40 cols
    } else if (usable_width >= 22) {
        bar_block_width = 1;
        bar_lead_spaces = 0;
        bar_trail_spaces = 0; // 1 col per bar ("%s") -> 20 cols
    } else {
        // Ultra-narrow window: scale rendered bars down to fit exactly
        bar_block_width = 1;
        bar_lead_spaces = 0;
        bar_trail_spaces = 0;
        num_bars = usable_width;
        if (num_bars < 4) num_bars = 4;
    }

    int col_width = bar_lead_spaces + bar_block_width + bar_trail_spaces;
    int total_width = num_bars * col_width;
    int horizontal_offset = (console_width - total_width) / 2;
    if (horizontal_offset < 1) horizontal_offset = 1;
    if (horizontal_offset + total_width >= console_width) {
        horizontal_offset = console_width - total_width - 1;
        if (horizontal_offset < 0) horizontal_offset = 0;
    }

    char left_pad[256] = {0};
    for (int p = 0; p < horizontal_offset && p < 255; p++) {
        left_pad[p] = ' ';
    }

    char buffer[32768];
    int offset = 0;
    
    // Reset cursor to top-left
    offset += sprintf(buffer + offset, "\033[H");
    
    // Render Debug Overlay only if explicitly enabled
    if (state->debug_mode) {
        offset += sprintf(buffer + offset, "%s\033[93m[DEBUG] WxH: %dx%d | sens: %.2f | nr: %d | bar_max: %.2f | dt: %.4fs (%.1f FPS)\033[0m\033[K\n",
                          left_pad, console_width, console_height, state->sens, state->noise_reduction, bar_max, dt, fps);
    }
    
    // Render vertical bars top-to-bottom
    for (int row = active_height - 1; row >= 0; row--) {
        int r_val, g_val, b_val;
        get_row_color(row, active_height, &r_val, &g_val, &b_val);
        
        offset += sprintf(buffer + offset, "%s", left_pad);
        offset += sprintf(buffer + offset, "\033[38;2;%d;%d;%dm", r_val, g_val, b_val);
        
        for (int col = 0; col < num_bars; col++) {
            int bar_idx = (num_bars == NUM_BARS) ? col : (col * (NUM_BARS - 1)) / (num_bars - 1);
            float h = state->bars[bar_idx];
            
            const char *blk;
            if (h >= row + 1) {
                blk = BLOCKS[8];
            } else if (h < row) {
                blk = BLOCKS[0];
            } else {
                float fraction = h - row;
                int idx = (int)(fraction * 8.0f + 0.5f);
                if (idx < 0) idx = 0;
                if (idx > 8) idx = 8;
                blk = BLOCKS[idx];
            }

            for (int s = 0; s < bar_lead_spaces; s++) {
                buffer[offset++] = ' ';
            }
            for (int b = 0; b < bar_block_width; b++) {
                offset += sprintf(buffer + offset, "%s", blk);
            }
            for (int s = 0; s < bar_trail_spaces; s++) {
                buffer[offset++] = ' ';
            }
        }
        offset += sprintf(buffer + offset, "\033[0m\033[K\n");
    }
    
    // 7. Visual Spacing / Breathing Room between bars and metadata line
    int space_lines = (console_height >= 16) ? 2 : 1;
    for (int s = 0; s < space_lines; s++) {
        offset += sprintf(buffer + offset, "\033[K\n");
    }
    
    // 8. Render only the Artist and Song Title (safely truncated and centered)
    char raw_info[256] = {0};
    int has_title = (title && strlen(title) > 0);
    int has_artist = (artist && strlen(artist) > 0);
    
    if (has_title && has_artist) {
        snprintf(raw_info, sizeof(raw_info), "🎵 %s - %s", artist, title);
    } else if (has_title) {
        snprintf(raw_info, sizeof(raw_info), "🎵 %s", title);
    } else if (has_artist) {
        snprintf(raw_info, sizeof(raw_info), "🎵 %s", artist);
    } else {
        snprintf(raw_info, sizeof(raw_info), "🎵 Esperando reproducción...");
    }
    
    char formatted_info[256] = {0};
    int max_meta_width = console_width - 2;
    if (max_meta_width < 10) max_meta_width = 10;
    truncate_to_display_width(raw_info, formatted_info, sizeof(formatted_info), max_meta_width);

    int info_display_width = utf8_display_width(formatted_info);
    int info_padding = (console_width - info_display_width) / 2;
    if (info_padding < 0) info_padding = 0;
    if (info_padding + info_display_width >= console_width) {
        info_padding = console_width - info_display_width - 1;
        if (info_padding < 0) info_padding = 0;
    }
    
    for (int p = 0; p < info_padding; p++) {
        buffer[offset++] = ' ';
    }
    offset += sprintf(buffer + offset, "\033[1;38;2;255;105;180m%s\033[0m\033[K\n", formatted_info);
    
    // Clear any extra residual lines below
    offset += sprintf(buffer + offset, "\033[K\n");
    
    printf("%s", buffer);
    fflush(stdout);
}
