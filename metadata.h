#ifndef METADATA_H
#define METADATA_H

// Read metadata from shared memory
// Returns 1 if successful, 0 otherwise
int read_shared_metadata(char *title, char *artist, int max_len);

#endif // METADATA_H
