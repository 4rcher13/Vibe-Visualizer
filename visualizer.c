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

int main() {
    // Set console code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Hide console cursor for cleaner render
    printf("\033[?25l");
    
    // Register control handler
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        fprintf(stderr, "Error: No se pudo registrar el manejador de consola.\n");
        return 1;
    }

    printf("Iniciando visualizador...\n");
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
    init_render_state(&renderState);
    
    char currentTitle[128] = "";
    char currentArtist[128] = "";
    
    // Clear screen initially
    printf("\033[2J");
    
    int frameCounter = 0;
    
    // Main visualizer loop
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
        
        // 4. Render frequencies and metadata
        render_frame(fftBuffer, FRAMES_PER_BUFFER, &renderState, currentTitle, currentArtist);
        
        // Sleep to throttle frame rate to ~60 FPS
        // WASAPI read blocks for ~23ms anyway (1024 / 44100), so sleeping 10ms is perfect
        Sleep(10);
    }

    // Cleanup resources
    cleanUp();
    return 0;
}