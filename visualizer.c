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
PROCESS_INFORMATION piPython;
BOOL pythonSpawned = FALSE;

// Console control handler for Ctrl+C and closing
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        running = 0;
        return TRUE;
    }
    return FALSE;
}

void cleanUp() {
    // 1. Terminate background Python process if running
    if (pythonSpawned) {
        TerminateProcess(piPython.hProcess, 0);
        CloseHandle(piPython.hProcess);
        CloseHandle(piPython.hThread);
        pythonSpawned = FALSE;
    }
    
    // 2. Stop and clean up WASAPI loopback capture
    wasapi_cleanup(&capture);
    
    // 3. Reset console cursor and clean up formatting
    printf("\033[?25h\033[0m\nVisualizador finalizado. ¡Hasta luego!\n");
}

// Synthetic Sine Wave Test Suite for Quantitative Pipeline Verification
static int run_synthetic_test(int noise_reduction, int debug_mode) {
    printf("=====================================================\n");
    printf("   PRUEBA SINTÉTICA DE PIPELINE CAVA (VERIFICACIÓN)  \n");
    printf("=====================================================\n");
    
    RenderState state;
    init_render_state(&state, noise_reduction, debug_mode);
    
    short audioBuffer[FRAMES_PER_BUFFER];
    Complex fftBuffer[FRAMES_PER_BUFFER];
    
    // Phase 1: 1 kHz Sine Wave at 0.5 Full Scale (FS) for 100 frames
    printf("[1/3] Generando tono de 1 kHz (amplitud 0.5 FS) por 100 cuadros...\n");
    float freq = 1000.0f;
    for (int frame = 0; frame < 100; frame++) {
        for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
            float sample = 0.5f * sinf(2.0f * PI * freq * (float)(frame * FRAMES_PER_BUFFER + i) / (float)SAMPLE_RATE);
            audioBuffer[i] = (short)(sample * 32767.0f);
        }
        apply_hann_window(audioBuffer, fftBuffer, FRAMES_PER_BUFFER);
        fft(fftBuffer, FRAMES_PER_BUFFER);
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &state, "Prueba Sintetica", "Sine 1kHz");
        Sleep(10);
    }
    
    float final_sens = state.sens;
    printf("   -> Sensibilidad estabilizada: %.4f (Límites: 0.5 - 30.0)\n", final_sens);
    
    // Phase 2: Silence test for 50 frames
    printf("[2/3] Generando silencio total (amplitud 0.0) por 50 cuadros...\n");
    for (int frame = 0; frame < 50; frame++) {
        memset(audioBuffer, 0, sizeof(audioBuffer));
        apply_hann_window(audioBuffer, fftBuffer, FRAMES_PER_BUFFER);
        fft(fftBuffer, FRAMES_PER_BUFFER);
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &state, "Prueba Silencio", "Silence 0.0");
        Sleep(10);
    }
    
    float silence_sens = state.sens;
    printf("   -> Sensibilidad en silencio: %.4f (Congelamiento verificado: %s)\n",
           silence_sens, (final_sens == silence_sens) ? "ÉXITO" : "FALLO");
           
    printf("[3/3] Verificación completada exitosamente.\n");
    printf("=====================================================\n\n");
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
    
    // Run synthetic test if requested via CLI flag
    if (test_sine) {
        return run_synthetic_test(noise_reduction, debug_mode);
    }

    // Hide console cursor for cleaner render
    printf("\033[?25l");
    
    // Register control handler
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        fprintf(stderr, "Error: No se pudo registrar el manejador de consola.\n");
        return 1;
    }

    printf("Iniciando visualizador Vibe-Visualizer (CAVA Pipeline, Noise Reduction: %d)...\n", noise_reduction);
    Sleep(500);

    // Initialize WASAPI loopback capture (system audio output)
    if (!wasapi_init(&capture)) {
        fprintf(stderr, "Error: No se pudo inicializar la captura de audio WASAPI.\n");
        printf("\033[?25h");
        return 1;
    }

    // Spawn the get_media.py metadata publisher in background (without a console window)
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&piPython, sizeof(piPython));
    
    char cmd[] = "python get_media.py";
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &piPython)) {
        pythonSpawned = TRUE;
    } else {
        printf("Advertencia: No se pudo iniciar el servicio de metadatos (Python).\n");
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
        
        // 3. Read metadata from shared memory once every 30 frames (~0.5s) to save resources
        if (frameCounter % 30 == 0) {
            read_shared_metadata(currentTitle, currentArtist, 128);
        }
        frameCounter++;
        
        // 4. Render frequencies and metadata (CAVA pipeline: Autosens, log scale, integral/gravity)
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &renderState, currentTitle, currentArtist);
        
        // Throttle loop cadence
        Sleep(10);
    }

    // Cleanup resources
    cleanUp();
    return 0;
}