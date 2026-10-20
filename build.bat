@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   Compilando Vibe-Visualizer
echo ============================================

:: 1. Intentar compilar Helper Nativo de Metadatos (C++/WinRT con MSVC)
where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 goto compile_helper

if exist "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    goto compile_helper
)

for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "& (Join-Path '%ProgramFiles(x86)%' 'Microsoft Visual Studio\Installer\vswhere.exe') -latest -prerelease -property installationPath"`) do (
    if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" (
        call "%%I\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        goto compile_helper
    )
)

echo [AVISO] Compilador MSVC no encontrado. media_helper.exe no sera compilado.
echo         El visualizador usara el script de Python si esta disponible.
goto compile_main

:compile_helper
echo [..] Compilando helper nativo de metadatos (media_helper.exe)...
cl.exe /nologo /EHsc /std:c++20 /O2 src\media_helper.cpp /Fe:media_helper.exe /Fo:media_helper.obj /link windowsapp.lib >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if exist media_helper.obj del media_helper.obj
    echo [OK] Helper nativo compilado: media_helper.exe
) else (
    echo [AVISO] Error al compilar media_helper.exe. Se usara fallback de Python si esta disponible.
)

:compile_main
:: 2. Compilar Visualizador Principal (GCC UCRT64)
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"

echo [..] Compilando visualizador principal (visualizador.exe)...
gcc -O2 -Isrc src\visualizer.c src\audio_capture.c src\render.c src\fft.c src\window.c src\metadata.c -o visualizador.exe -lole32 -lm

if %ERRORLEVEL% equ 0 (
    echo [OK] Compilacion exitosa: visualizador.exe
) else (
    echo [ERROR] Error durante la compilacion de visualizador.exe.
)

echo ============================================
