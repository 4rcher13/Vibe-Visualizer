#include "metadata.h"
#include <windows.h>
#include <stdio.h>

#define SHM_NAME "VibeVisualizerMetadata"
#define SHM_SIZE 512

int read_shared_metadata(char *title, char *artist, int max_len) {
    HANDLE hMapFile = OpenFileMappingA(
        FILE_MAP_READ,
        FALSE,
        SHM_NAME
    );
    
    if (hMapFile == NULL) {
        return 0; // Not running or not created yet
    }
    
    char *pBuf = (char*)MapViewOfFile(
        hMapFile,
        FILE_MAP_READ,
        0,
        0,
        SHM_SIZE
    );
    
    if (pBuf == NULL) {
        CloseHandle(hMapFile);
        return 0;
    }
    
    // Format is: Title\nArtist\0
    // Parse it
    int i = 0;
    int j = 0;
    
    // Read title
    while (pBuf[i] != '\n' && pBuf[i] != '\0' && i < SHM_SIZE - 1 && j < max_len - 1) {
        title[j++] = pBuf[i++];
    }
    title[j] = '\0';
    
    if (pBuf[i] == '\n') {
        i++;
    }
    
    // Read artist
    j = 0;
    while (pBuf[i] != '\0' && i < SHM_SIZE - 1 && j < max_len - 1) {
        artist[j++] = pBuf[i++];
    }
    artist[j] = '\0';
    
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    
    return 1;
}
