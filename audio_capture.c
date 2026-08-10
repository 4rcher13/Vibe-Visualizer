// ============================================================================
// WASAPI Loopback Audio Capture Implementation
//
// Captures system audio output using Windows Audio Session API (WASAPI)
// in loopback mode. Opens the default render device (eRender) with
// AUDCLNT_STREAMFLAGS_LOOPBACK to tap into the audio the user hears.
//
// Key design decisions:
// - Ring buffer absorbs variable-size WASAPI packets so the FFT always
//   gets exactly FRAMES_PER_BUFFER samples.
// - Format detection handles both WAVEFORMATEXTENSIBLE and plain WAVEFORMATEX,
//   supporting IEEE float and PCM integer with any channel count.
// - AUDCLNT_BUFFERFLAGS_SILENT is handled explicitly (no read from pData).
// - GUIDs for ksmedia SubFormat are defined locally to avoid -lksuser.
// ============================================================================

// INITGUID must come before any Windows header so that GUIDs like
// IID_IAudioClient, CLSID_MMDeviceEnumerator, etc. are instantiated
// in this translation unit. Without this, GCC/MinGW produces
// "undefined reference to IID_IAudioClient" at link time.
#define INITGUID

#include "audio_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <objbase.h>
#include <functiondiscoverykeys_devpkey.h>

// ---------------------------------------------------------------------------
// ksmedia SubFormat GUIDs — defined locally to avoid linking -lksuser
// ---------------------------------------------------------------------------
static const GUID MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID MY_KSDATAFORMAT_SUBTYPE_PCM = {
    0x00000001, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// Ring buffer capacity: 4x the typical FFT window size gives ~93ms of headroom
// at 44100 Hz, which is more than enough to absorb WASAPI packet jitter.
#define RING_BUFFER_FRAMES  4096

// ---------------------------------------------------------------------------
// Helper: mix one multi-channel frame down to mono float
// ---------------------------------------------------------------------------
static float mix_frame_to_mono_float(const BYTE *pData, int frameIndex,
                                     int nChannels, int bytesPerSample,
                                     int isFloat)
{
    float sum = 0.0f;
    int offset = frameIndex * nChannels * bytesPerSample;

    for (int ch = 0; ch < nChannels; ch++) {
        float sample = 0.0f;
        const BYTE *p = pData + offset + ch * bytesPerSample;

        if (isFloat) {
            // IEEE 754 float32 (most common WASAPI format)
            sample = *(const float *)p;
        } else {
            // PCM integer — handle 16-bit and 24/32-bit
            if (bytesPerSample == 2) {
                sample = (float)(*(const short *)p) / 32768.0f;
            } else if (bytesPerSample == 3) {
                // 24-bit little-endian: sign-extend to 32-bit
                int val = (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8) |
                                ((unsigned int)p[2] << 16));
                if (val & 0x800000) val |= ~0xFFFFFF; // sign extend
                sample = (float)val / 8388608.0f;
            } else if (bytesPerSample == 4) {
                sample = (float)(*(const int *)p) / 2147483648.0f;
            }
        }
        sum += sample;
    }

    return sum / (float)nChannels;
}

// ---------------------------------------------------------------------------
// Helper: write one mono float sample into the ring buffer
// ---------------------------------------------------------------------------
static void ring_write(WasapiCapture *cap, float sample)
{
    cap->ringBuf[cap->ringHead] = sample;
    cap->ringHead = (cap->ringHead + 1) % cap->ringCapacity;

    if (cap->ringCount < cap->ringCapacity) {
        cap->ringCount++;
    } else {
        // Overrun: advance tail (discard oldest sample)
        cap->ringTail = (cap->ringTail + 1) % cap->ringCapacity;
    }
}

// ---------------------------------------------------------------------------
// Helper: read one mono float sample from the ring buffer
// ---------------------------------------------------------------------------
static float ring_read(WasapiCapture *cap)
{
    float sample = cap->ringBuf[cap->ringTail];
    cap->ringTail = (cap->ringTail + 1) % cap->ringCapacity;
    cap->ringCount--;
    return sample;
}

// ---------------------------------------------------------------------------
// wasapi_init
// ---------------------------------------------------------------------------
int wasapi_init(WasapiCapture *cap)
{
    memset(cap, 0, sizeof(*cap));

    // 1. Initialize COM (multithreaded)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "Error: CoInitializeEx falló (0x%08lx)\n", hr);
        return 0;
    }

    // 2. Create device enumerator
    IMMDeviceEnumerator *pEnumerator = NULL;
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
        &IID_IMMDeviceEnumerator, (void **)&pEnumerator
    );
    if (FAILED(hr)) {
        fprintf(stderr, "Error: No se pudo crear IMMDeviceEnumerator (0x%08lx)\n", hr);
        CoUninitialize();
        return 0;
    }

    // 3. Get default RENDER endpoint (speakers/headphones — NOT microphone)
    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
        pEnumerator, eRender, eConsole, &cap->pDevice
    );
    pEnumerator->lpVtbl->Release(pEnumerator);
    if (FAILED(hr)) {
        fprintf(stderr, "Error: No se encontró dispositivo de salida de audio (0x%08lx)\n", hr);
        CoUninitialize();
        return 0;
    }

    // 4. Activate IAudioClient
    hr = cap->pDevice->lpVtbl->Activate(
        cap->pDevice, &IID_IAudioClient, CLSCTX_ALL, NULL,
        (void **)&cap->pAudioClient
    );
    if (FAILED(hr)) {
        fprintf(stderr, "Error: No se pudo activar IAudioClient (0x%08lx)\n", hr);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }

    // 5. Get mix format and detect sample type
    hr = cap->pAudioClient->lpVtbl->GetMixFormat(cap->pAudioClient, &cap->pWaveFormat);
    if (FAILED(hr)) {
        fprintf(stderr, "Error: GetMixFormat falló (0x%08lx)\n", hr);
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pAudioClient = NULL;
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }

    WAVEFORMATEX *wfx = cap->pWaveFormat;
    cap->nChannels     = wfx->nChannels;
    cap->sampleRate    = (int)wfx->nSamplesPerSec;
    cap->bytesPerSample = wfx->wBitsPerSample / 8;

    // Determine if float or PCM by inspecting the format tag / SubFormat GUID
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *wfxe = (WAVEFORMATEXTENSIBLE *)wfx;
        if (IsEqualGUID(&wfxe->SubFormat, &MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            cap->isFloat = 1;
        } else if (IsEqualGUID(&wfxe->SubFormat, &MY_KSDATAFORMAT_SUBTYPE_PCM)) {
            cap->isFloat = 0;
        } else {
            // Unknown SubFormat — assume float (most common)
            fprintf(stderr, "Advertencia: SubFormat desconocido, asumiendo float.\n");
            cap->isFloat = 1;
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        cap->isFloat = 1;
    } else {
        // WAVE_FORMAT_PCM or other
        cap->isFloat = 0;
    }

    fprintf(stderr, "Audio: %d Hz, %d canales, %d bits, %s\n",
            cap->sampleRate, cap->nChannels, wfx->wBitsPerSample,
            cap->isFloat ? "float" : "PCM");

    // 6. Initialize audio client in SHARED mode with LOOPBACK flag
    // hnsBufferDuration = 200000 (20ms in 100ns units) — a safe value for shared mode
    hr = cap->pAudioClient->lpVtbl->Initialize(
        cap->pAudioClient,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        200000,     // hnsBufferDuration: 20ms
        0,          // hnsPeriodicity: must be 0 for shared mode
        cap->pWaveFormat,
        NULL        // AudioSessionGuid
    );
    if (FAILED(hr)) {
        fprintf(stderr, "Error: IAudioClient::Initialize falló (0x%08lx)\n", hr);
        CoTaskMemFree(cap->pWaveFormat);
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pWaveFormat = NULL;
        cap->pAudioClient = NULL;
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }

    // 7. Get capture service
    hr = cap->pAudioClient->lpVtbl->GetService(
        cap->pAudioClient, &IID_IAudioCaptureClient,
        (void **)&cap->pCaptureClient
    );
    if (FAILED(hr)) {
        fprintf(stderr, "Error: GetService(IAudioCaptureClient) falló (0x%08lx)\n", hr);
        CoTaskMemFree(cap->pWaveFormat);
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pWaveFormat = NULL;
        cap->pAudioClient = NULL;
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }

    // 8. Start the audio stream — without this, GetBuffer returns nothing
    hr = cap->pAudioClient->lpVtbl->Start(cap->pAudioClient);
    if (FAILED(hr)) {
        fprintf(stderr, "Error: IAudioClient::Start falló (0x%08lx)\n", hr);
        cap->pCaptureClient->lpVtbl->Release(cap->pCaptureClient);
        CoTaskMemFree(cap->pWaveFormat);
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pCaptureClient = NULL;
        cap->pWaveFormat = NULL;
        cap->pAudioClient = NULL;
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }

    // 9. Allocate ring buffer
    cap->ringCapacity = RING_BUFFER_FRAMES;
    cap->ringBuf = (float *)calloc(cap->ringCapacity, sizeof(float));
    if (!cap->ringBuf) {
        fprintf(stderr, "Error: No se pudo asignar ring buffer.\n");
        cap->pAudioClient->lpVtbl->Stop(cap->pAudioClient);
        cap->pCaptureClient->lpVtbl->Release(cap->pCaptureClient);
        CoTaskMemFree(cap->pWaveFormat);
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pCaptureClient = NULL;
        cap->pWaveFormat = NULL;
        cap->pAudioClient = NULL;
        cap->pDevice = NULL;
        CoUninitialize();
        return 0;
    }
    cap->ringHead  = 0;
    cap->ringTail  = 0;
    cap->ringCount = 0;

    return 1;
}

// ---------------------------------------------------------------------------
// wasapi_read
// ---------------------------------------------------------------------------
int wasapi_read(WasapiCapture *cap, short *buffer, int frames)
{
    HRESULT hr;
    UINT32 packetLength = 0;

    // Phase 1: Drain ALL available WASAPI packets into the ring buffer.
    // There may be zero, one, or multiple packets queued up.
    hr = cap->pCaptureClient->lpVtbl->GetNextPacketSize(
        cap->pCaptureClient, &packetLength
    );
    if (FAILED(hr)) {
        // On error, fill output with silence and bail
        memset(buffer, 0, frames * sizeof(short));
        return 0;
    }

    while (packetLength > 0) {
        BYTE   *pData = NULL;
        UINT32  numFramesAvailable = 0;
        DWORD   flags = 0;

        hr = cap->pCaptureClient->lpVtbl->GetBuffer(
            cap->pCaptureClient,
            &pData,
            &numFramesAvailable,
            &flags,
            NULL,   // pu64DevicePosition
            NULL    // pu64QPCPosition
        );
        if (FAILED(hr)) {
            break;
        }

        // Process each frame in this packet
        for (UINT32 f = 0; f < numFramesAvailable; f++) {
            float monoSample = 0.0f;

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // pData may be NULL or garbage — treat as silence
                monoSample = 0.0f;
            } else {
                // Mix all channels down to one mono float
                monoSample = mix_frame_to_mono_float(
                    pData, (int)f,
                    cap->nChannels, cap->bytesPerSample, cap->isFloat
                );
            }

            ring_write(cap, monoSample);
        }

        cap->pCaptureClient->lpVtbl->ReleaseBuffer(
            cap->pCaptureClient, numFramesAvailable
        );

        // Check if there are more packets
        hr = cap->pCaptureClient->lpVtbl->GetNextPacketSize(
            cap->pCaptureClient, &packetLength
        );
        if (FAILED(hr)) {
            break;
        }
    }

    // Phase 2: Copy 'frames' samples from ring buffer to output.
    // Convert float -> int16 with clamp.
    int full = (cap->ringCount >= frames) ? 1 : 0;
    int available = (cap->ringCount < frames) ? cap->ringCount : frames;

    for (int i = 0; i < available; i++) {
        float sample = ring_read(cap);
        // Clamp to [-1.0, 1.0]
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i] = (short)(sample * 32767.0f);
    }

    // Pad remainder with silence if not enough data
    if (available < frames) {
        memset(buffer + available, 0, (frames - available) * sizeof(short));
    }

    return full;
}

// ---------------------------------------------------------------------------
// wasapi_cleanup
// ---------------------------------------------------------------------------
void wasapi_cleanup(WasapiCapture *cap)
{
    // Stop the stream first
    if (cap->pAudioClient) {
        cap->pAudioClient->lpVtbl->Stop(cap->pAudioClient);
    }

    // Release COM objects in reverse acquisition order
    if (cap->pCaptureClient) {
        cap->pCaptureClient->lpVtbl->Release(cap->pCaptureClient);
        cap->pCaptureClient = NULL;
    }

    // Free the mix format (allocated by CoTaskMemAlloc inside GetMixFormat)
    if (cap->pWaveFormat) {
        CoTaskMemFree(cap->pWaveFormat);
        cap->pWaveFormat = NULL;
    }

    if (cap->pAudioClient) {
        cap->pAudioClient->lpVtbl->Release(cap->pAudioClient);
        cap->pAudioClient = NULL;
    }

    if (cap->pDevice) {
        cap->pDevice->lpVtbl->Release(cap->pDevice);
        cap->pDevice = NULL;
    }

    // Free ring buffer
    if (cap->ringBuf) {
        free(cap->ringBuf);
        cap->ringBuf = NULL;
    }

    CoUninitialize();
}
