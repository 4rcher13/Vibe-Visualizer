#ifndef METADATA_H
#define METADATA_H

#define SHM_NAME "VibeVisualizerMetadata"
#define SHM_SIZE 512

// Lee los metadatos desde la memoria compartida
// Retorna 1 si es exitoso, 0 si no
int read_shared_metadata(char *title, char *artist, int max_len);

#endif // METADATA_H
