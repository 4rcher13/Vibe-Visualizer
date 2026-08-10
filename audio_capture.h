#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

// ============================================================================
// WASAPI Loopback Audio Capture
// Captures system output audio (what you hear) via eRender + loopback flag.
// ============================================================================

typedef struct {
    // COM interface pointers
    IMMDevice           *pDevice;
    IAudioClient        *pAudioClient;
    IAudioCaptureClient *pCaptureClient;
    WAVEFORMATEX        *pWaveFormat;       // Mix format allocated by GetMixFormat

    // Ring buffer (mono float) to absorb variable-size WASAPI packets
    float  *ringBuf;
    int     ringCapacity;   // Total capacity in frames
    int     ringHead;       // Write index
    int     ringTail;       // Read index
    int     ringCount;      // Frames currently available

    // Format metadata (read from system mix format)
    int     nChannels;      // Actual channel count (2, 6, 8, etc.)
    int     sampleRate;     // Actual sample rate (44100, 48000, etc.)
    int     isFloat;        // 1 = IEEE float, 0 = PCM integer
    int     bytesPerSample; // Bytes per sample per channel (2, 3, 4, etc.)
} WasapiCapture;

// Initialize WASAPI loopback capture.
// Opens the default render endpoint with AUDCLNT_STREAMFLAGS_LOOPBACK,
// allocates the ring buffer, and starts the audio client.
// Returns 1 on success, 0 on failure (error printed to stderr).
int wasapi_init(WasapiCapture *cap);

// Read exactly 'frames' mono int16 samples into 'buffer'.
// Internally drains all available WASAPI packets into the ring buffer,
// then copies 'frames' samples to the output.
// Returns 1 if the full request was satisfied, 0 if padded with silence.
int wasapi_read(WasapiCapture *cap, short *buffer, int frames);

// Stop the stream and release all COM objects and memory.
void wasapi_cleanup(WasapiCapture *cap);

#endif // AUDIO_CAPTURE_H
