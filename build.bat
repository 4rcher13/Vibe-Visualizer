@echo off
setlocal enabledelayedexpansion

set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"

echo ============================================
echo   Compilando Vibe-Visualizer (UCRT64 GCC)
echo ============================================

gcc -O2 -Isrc src\visualizer.c src\audio_capture.c src\render.c src\fft.c src\window.c src\metadata.c -o visualizador.exe -lole32 -lm

if %ERRORLEVEL% equ 0 (
    echo [OK] Compilacion exitosa: visualizador.exe
) else (
    echo [ERROR] Error durante la compilacion.
)
