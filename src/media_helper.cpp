#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#pragma comment(lib, "windowsapp.lib")

#define SHM_NAME "VibeVisualizerMetadata"
#define SHM_SIZE 512

using namespace winrt;
using namespace Windows::Media::Control;

// Escribe los metadatos formateados como: Title\nArtist\0
static void write_metadata(char* pBuf, const std::string& title, const std::string& artist) {
    if (!pBuf) return;

    std::string payload = title + "\n" + artist;
    if (payload.length() >= SHM_SIZE) {
        payload = payload.substr(0, SHM_SIZE - 1);
    }

    memset(pBuf, 0, SHM_SIZE);
    memcpy(pBuf, payload.c_str(), payload.length());
}

int main(int argc, char* argv[]) {
    // Si se pasa el PID del visualizador padre, vigilar su ciclo de vida
    DWORD parentPid = 0;
    if (argc > 1) {
        try {
            parentPid = (DWORD)std::stoul(argv[1]);
        } catch (...) {
            parentPid = 0;
        }
    }

    HANDLE hParent = NULL;
    if (parentPid != 0) {
        hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    }

    // Crear o abrir la memoria compartida
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        SHM_SIZE,
        SHM_NAME
    );

    if (hMapFile == NULL) {
        if (hParent) CloseHandle(hParent);
        return 1;
    }

    char* pBuf = (char*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    if (pBuf == NULL) {
        CloseHandle(hMapFile);
        if (hParent) CloseHandle(hParent);
        return 1;
    }

    // Inicializar memoria compartida con datos vacios
    write_metadata(pBuf, "", "");

    try {
        init_apartment(apartment_type::multi_threaded);
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

        std::string lastTitle = "";
        std::string lastArtist = "";

        while (true) {
            // Si el proceso padre termino, salir inmediatamente para evitar procesos huerfanos
            if (hParent != NULL) {
                DWORD waitRes = WaitForSingleObject(hParent, 0);
                if (waitRes == WAIT_OBJECT_0) {
                    break;
                }
            }

            try {
                if (manager) {
                    auto session = manager.GetCurrentSession();
                    if (session) {
                        auto props = session.TryGetMediaPropertiesAsync().get();
                        if (props) {
                            std::string title = to_string(props.Title());
                            std::string artist = to_string(props.Artist());

                            if (title != lastTitle || artist != lastArtist) {
                                lastTitle = title;
                                lastArtist = artist;
                                write_metadata(pBuf, title, artist);
                            }
                        }
                    } else {
                        if (!lastTitle.empty() || !lastArtist.empty()) {
                            lastTitle = "";
                            lastArtist = "";
                            write_metadata(pBuf, "", "");
                        }
                    }
                }
            } catch (...) {
                // Ignorar excepciones transitorias de WinRT / COM al pausar o cambiar de pista
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

    } catch (...) {
        // En caso de error fatal de inicializacion WinRT, limpiar y salir
    }

    if (pBuf) UnmapViewOfFile(pBuf);
    if (hMapFile) CloseHandle(hMapFile);
    if (hParent) CloseHandle(hParent);

    return 0;
}
