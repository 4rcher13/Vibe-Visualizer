#include <stdio.h>
#include <stdlib.h>
#include <windows.h> 
#include <time.h>
#include <math.h>
#include <portaudio.h> 

#define Num_bars 10
#define Max_height 8
#define simple_rate 44100 
#define frames_per_buffer 1024 

//Mover el cursor
void resetearCusor(){
    COORD coord = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

//Limpiar pantalla en Windows
void limpiarPantalla() {
    system("cls");
}

int main() {
    SetConsoleOutputCP(CP_UTF8); // Configurar la consola para UTF-8
    SetConsoleCP(CP_UTF8);
    // Inicializamos la semilla aleatoria UNA sola vez al inicio
    srand(time(NULL));

    int alturas[Num_bars];
    PaStream *stream; //Apuntador a stream
    PaError err;

    limpiarPantalla();
    printf("Inciciando visualizador...\n");
    Sleep(1000);

    // 1. Inicializar PortAudio (AFUERA del bucle)
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "Error al inicializar PortAudio: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    // 2. Abrir flujo: 1 Entrada (Microfono), 0 Salidas
    err = Pa_OpenDefaultStream(&stream, 1, 0, paInt16, simple_rate, frames_per_buffer, NULL, NULL);
    if (err != paNoError) {
        fprintf(stderr, "Error al abrir el flujo de audio: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return 1;
    }

    // 3. Iniciar flujo
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "Error al iniciar el flujo de audio: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    short buffer[frames_per_buffer];

    limpiarPantalla();
    printf("Escuchando audio... Presiona Ctrl+C para salir.\n\n");
    Sleep(1000);

    limpiarPantalla();

    // BUCLE PRINCIPAL 
    while (1) {
        // Leer datos del micrófono de forma síncrona
        Pa_ReadStream(stream, buffer, frames_per_buffer);
        
        resetearCursor();

        // Calcular el volumen promedio del audio capturado
        float volumen_promedio = 0;
        for (int i = 0; i < frames_per_buffer; i++) {
            volumen_promedio += abs(buffer[i]);
        }
        volumen_promedio /= frames_per_buffer; // Promedio de las 1024 muestras

        // Asignar alturas a las 10 barras basándonos en el volumen real
        for (int i = 0; i < Num_bars; i++) {
            // El volumen promedio suele rondar entre 0 y 3000 según el micro
            int val = (int)(volumen_promedio / 150) + (rand() % 3 - 1); 
            
            if (val < 0) val = 0;
            if (val > Max_height) val = Max_height;
            alturas[i] = val;
        }

        // Dibujar barras verticales
        for (int fila = Max_height; fila > 0; fila--) {
            for (int col = 0; col < Num_bars; col++) {
                if (alturas[col] >= fila) {
                    printf(" [##] ");
                } else {
                    printf("    ");
                }
            }
            printf("\n");
        }
        
        printf("\n Visualizador conectado al Micrófono \n");

        Sleep(30); // ~30 FPS, ideal para que PortAudio no se sature leyendo
    }
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}