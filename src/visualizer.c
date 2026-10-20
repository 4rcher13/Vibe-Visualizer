#include <stdio.h>
#include <stdlib.h>
#include <windows.h> 
#include <time.h>
#include <math.h>

#include "fft.h"
#include "window.h"
#include "metadata.h"
#include "render.h"
#include "audio_capture.h"

#define SAMPLE_RATE 44100 
#define FRAMES_PER_BUFFER 1024 
#define PI 3.14159265358979323846f

// Global variables for cleanup in control handler
volatile int running = 1;
WasapiCapture capture;
PROCESS_INFORMATION piHelper;
BOOL helperSpawned = FALSE;

// Console control handler for Ctrl+C and closing
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        running = 0;
        return TRUE;
    }
    return FALSE;
}

void cleanUp() {
    // 1. Terminate background metadata helper process if running
    if (helperSpawned) {
        TerminateProcess(piHelper.hProcess, 0);
        CloseHandle(piHelper.hProcess);
        CloseHandle(piHelper.hThread);
        helperSpawned = FALSE;
    }
    
    // 2. Stop and clean up WASAPI loopback capture
    wasapi_cleanup(&capture);
    
    // 3. Reset console cursor and clean up formatting
    printf("\033[?25h\033[0m\n");
}

// Synthetic Sine Wave Test Suite for Pipeline Verification
static int run_synthetic_test(int noise_reduction, int debug_mode) {
    RenderState state;
    init_render_state(&state, noise_reduction, debug_mode);
    
    short audioBuffer[FRAMES_PER_BUFFER];
    Complex fftBuffer[FRAMES_PER_BUFFER];
    
    float freq = 1000.0f;
    for (int frame = 0; frame < 100; frame++) {
        for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
            float sample = 0.5f * sinf(2.0f * PI * freq * (float)(frame * FRAMES_PER_BUFFER + i) / (float)SAMPLE_RATE);
            audioBuffer[i] = (short)(sample * 32767.0f);
        }
        apply_hann_window(audioBuffer, fftBuffer, FRAMES_PER_BUFFER);
        fft(fftBuffer, FRAMES_PER_BUFFER);
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &state, "Sine 1kHz", "Prueba Sintetica");
        Sleep(10);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int noise_reduction = 77;
    int debug_mode = 0;
    int test_sine = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "--test-sine") == 0) {
            test_sine = 1;
        } else {
            int val = atoi(argv[i]);
            if (val >= 0 && val <= 100) {
                noise_reduction = val;
            }
        }
    }

    // Set console code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Enable ANSI/VT escape sequence processing on console output handle
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    
    // Run synthetic test if requested
    if (test_sine) {
        return run_synthetic_test(noise_reduction, debug_mode);
    }

    // Hide console cursor for clean render
    printf("\033[?25l");
    
    // Register control handler
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        return 1;
    }

    // Initialize WASAPI loopback capture (system audio output)
    if (!wasapi_init(&capture)) {
        printf("\033[?25h");
        return 1;
    }

    // Determine metadata service: prefer native media_helper.exe, fallback to Python
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&piHelper, sizeof(piHelper));
    
    char cmd[512];
    DWORD helperAttr = GetFileAttributesA("media_helper.exe");
    if (helperAttr != INVALID_FILE_ATTRIBUTES && !(helperAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(cmd, sizeof(cmd), "media_helper.exe %lu", GetCurrentProcessId());
    } else {
        DWORD venvAttr = GetFileAttributesA(".venv\\Scripts\\python.exe");
        if (venvAttr != INVALID_FILE_ATTRIBUTES && !(venvAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(cmd, sizeof(cmd), ".venv\\Scripts\\python.exe scripts\\get_media.py");
        } else {
            snprintf(cmd, sizeof(cmd), "python scripts/get_media.py");
        }
    }

    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &piHelper)) {
        helperSpawned = TRUE;
    } else {
        // Fallback attempt to root path if script is located in current directory
        char fallbackCmd[] = "python get_media.py";
        if (CreateProcessA(NULL, fallbackCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &piHelper)) {
            helperSpawned = TRUE;
        }
    }

    // Allocate memory buffers
    short audioBuffer[FRAMES_PER_BUFFER];
    Complex fftBuffer[FRAMES_PER_BUFFER];
    RenderState renderState;
    init_render_state(&renderState, noise_reduction, debug_mode);
    
    char currentTitle[128] = "";
    char currentArtist[128] = "";
    
    // Clear screen initially
    printf("\033[2J");
    
    int frameCounter = 0;
    
    // Main visualizer loop (Synchronous single-threaded polling)
    while (running) {
        // Read audio samples from WASAPI loopback (system audio)
        wasapi_read(&capture, audioBuffer, FRAMES_PER_BUFFER);
        
        // 1. Apply Hann Window to reduce spectral leakage
        apply_hann_window(audioBuffer, fftBuffer, FRAMES_PER_BUFFER);
        
        // 2. Compute FFT Cooley-Tukey
        fft(fftBuffer, FRAMES_PER_BUFFER);
        
        // 3. Read metadata from shared memory once every 30 frames (~0.5s)
        if (frameCounter % 30 == 0) {
            read_shared_metadata(currentTitle, currentArtist, 128);
        }
        frameCounter++;
        
        // 4. Render frequencies and metadata
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &renderState, currentTitle, currentArtist);
        
        // Throttle loop cadence
        Sleep(10);
    }

    // Cleanup resources
    cleanUp();
    return 0;
}
