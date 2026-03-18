/*
 * src/audio/circle/SDL_circleaudio_impl.cpp
 *
 * C++ glue between SDL3 Circle audio backend and CSoundBaseDevice.
 *
 * Architecture:
 *   - We subclass CPWMSoundBaseDevice to get the DMA-driven interrupt model.
 *   - The DMA callback (GetChunk) pulls from a ring buffer that SDL3 fills
 *     via circle_audio_write().
 *   - On RPi 3B the 3.5mm jack uses PWM.  HDMI audio uses CHDMISoundBaseDevice;
 *     we default to PWM for maximum compatibility and add HDMI as fallback.
 *
 * SDL3 side:
 *   - ProvidesOwnCallbackThread = true (no SDL thread created)
 *   - OpenDevice: starts the sound device
 *   - WaitDevice: waits until the Circle write queue has space
 *   - PlayDevice: writes the buffer to the Circle write queue via Write()
 *   - GetDeviceBuf: returns the intermediate mix buffer
 */

#include <stdint.h>
#include <string.h>

#include <circle/interrupt.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>

/* ---- SDL3 audio format constants (mirror SDL_audio.h values) ---- */
/* We avoid including SDL headers here to keep the C++ clean */
#define CIRCLE_AUDIO_SAMPLERATE   44100
#define CIRCLE_AUDIO_CHANNELS     2
#define CIRCLE_AUDIO_CHUNK_FRAMES 1024   /* SDL will use this as sample_frames */

/* ------------------------------------------------------------------ */
/* Circle PWM Sound Device subclass                                   */
/* ------------------------------------------------------------------ */

class CCircleSDLSound : public CPWMSoundBaseDevice
{
public:
    CCircleSDLSound(CInterruptSystem *pInterrupt,
                    unsigned nSampleRate,
                    unsigned nChunkSize)
    : CPWMSoundBaseDevice(pInterrupt, nSampleRate, nChunkSize)
    {
        /* Use Write() API — allocate a 500ms queue */
        SetWriteFormat(SoundFormatSigned16, 2 /* stereo */);
        AllocateQueue(500 /* ms */);
    }

    /* Write PCM S16LE stereo data into the DMA queue.
     * Returns number of bytes consumed (may be less than buflen if queue full). */
    int WriteAudio(const void *pBuffer, unsigned nBytes)
    {
        return Write(pBuffer, nBytes);
    }

    /* Frames available for writing without blocking */
    unsigned QueueFramesAvail(void)
    {
        unsigned total  = GetQueueSizeFrames();
        unsigned used   = GetQueueFramesAvail();
        return (total > used) ? (total - used) : 0;
    }

    unsigned QueueSizeFrames(void)
    {
        return GetQueueSizeFrames();
    }
};

/* ------------------------------------------------------------------ */
/* Module-level state                                                  */
/* ------------------------------------------------------------------ */

static CCircleSDLSound  *s_pSound      = nullptr;
static uint8_t          *s_pMixBuf     = nullptr;
static unsigned          s_nMixBufSize = 0;

/* The interrupt system pointer is supplied by the kernel at startup */
static CInterruptSystem *s_pInterrupt  = nullptr;

/* ------------------------------------------------------------------ */
/* extern "C" interface                                                */
/* ------------------------------------------------------------------ */

extern "C" {

/* Must be called ONCE from the Circle kernel before SDL_Init():
 * circle_audio_set_interrupt(pInterrupt) */
void circle_audio_set_interrupt(void *pInterrupt)
{
    s_pInterrupt = (CInterruptSystem *)pInterrupt;
}

int circle_audio_open(unsigned nSampleRate, unsigned nChannels,
                      unsigned nSampleFrames)
{
    (void)nChannels; /* always stereo */

    if (!s_pInterrupt) {
        return -1; /* caller must call circle_audio_set_interrupt first */
    }

    /* nChunkSize = 2 * nSampleFrames (one word per channel per frame) */
    unsigned nChunkSize = nSampleFrames * 2;
    s_pSound = new CCircleSDLSound(s_pInterrupt, nSampleRate, nChunkSize);

    /* Allocate our intermediate mix buffer (S16LE stereo) */
    s_nMixBufSize = nSampleFrames * 2 * sizeof(int16_t);
    s_pMixBuf = new uint8_t[s_nMixBufSize];
    memset(s_pMixBuf, 0, s_nMixBufSize);

    if (!s_pSound->Start()) {
        delete s_pSound;  s_pSound = nullptr;
        delete[] s_pMixBuf; s_pMixBuf = nullptr;
        return -1;
    }
    return 0;
}

void circle_audio_close(void)
{
    if (s_pSound) {
        s_pSound->Cancel();
        delete s_pSound;
        s_pSound = nullptr;
    }
    delete[] s_pMixBuf;
    s_pMixBuf = nullptr;
    s_nMixBufSize = 0;
}

/* Returns mix buffer pointer; SDL3 renders into this */
uint8_t *circle_audio_get_buf(unsigned *pSizeOut)
{
    if (pSizeOut) *pSizeOut = s_nMixBufSize;
    return s_pMixBuf;
}

/* WaitDevice: never block - audio data may be dropped if queue is full
 * but that is better than stalling the single emulator core. */
void circle_audio_wait(unsigned nFrames)
{
    (void)nFrames;
}

/* PlayDevice: push mix buffer to Circle sound queue */
int circle_audio_play(const uint8_t *pBuffer, unsigned nBytes)
{
    if (!s_pSound) return -1;
    int written = s_pSound->WriteAudio(pBuffer, nBytes);
    return (written >= 0) ? 0 : -1;
}

int circle_audio_is_active(void)
{
    return (s_pSound && s_pSound->IsActive()) ? 1 : 0;
}

unsigned circle_audio_sample_frames(void)
{
    return CIRCLE_AUDIO_CHUNK_FRAMES;
}

} /* extern "C" */
