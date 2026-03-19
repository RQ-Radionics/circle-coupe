// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio via VCHIQ (HDMI/headphone)
//
// Same approach as bmc64: CVCHIQSoundBaseDevice with GetChunk override.
// VCHIQ talks to the GPU VideoCore which handles HDMI audio output.
//
// DMA at 44100Hz is the natural master clock for emulation speed.

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <vc4/sound/vchiqsoundbasedevice.h>
#include <vc4/vchiq/vchiqdevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Ring buffer (lock-free: core 1 writes, core 0 VCHIQ reads) ---------

static constexpr unsigned RING_FRAMES  = 4096;
static constexpr unsigned RING_CH      = 2;
static constexpr unsigned RING_SAMPLES = RING_FRAMES * RING_CH;

static s16      s_ring[RING_SAMPLES];
static volatile unsigned s_ring_head = 0;
static volatile unsigned s_ring_tail = 0;

static inline unsigned ring_used() {
    return (s_ring_head - s_ring_tail + RING_SAMPLES) % RING_SAMPLES;
}

// ---- VCHIQ sound device with GetChunk override (like bmc64) -------------

class CircleVCHIQSound : public CVCHIQSoundBaseDevice
{
public:
    CircleVCHIQSound(CVCHIQDevice *pVCHIQ, unsigned nSampleRate,
                     unsigned nChunkSize,
                     TVCHIQSoundDestination dest = VCHIQSoundDestinationAuto)
    : CVCHIQSoundBaseDevice(pVCHIQ, nSampleRate, nChunkSize, dest) {}

protected:
    unsigned GetChunk(s16 *pBuffer, unsigned nChunkSize) override
    {
        // nChunkSize = number of samples (not frames) to fill
        unsigned filled = 0;
        asm volatile("dmb" ::: "memory");
        while (filled < nChunkSize && ring_used() > 0)
        {
            unsigned t = s_ring_tail;
            pBuffer[filled++] = s_ring[t];
            t = (t + 1) % RING_SAMPLES;
            asm volatile("dmb" ::: "memory");
            s_ring_tail = t;
        }
        // Fill rest with silence if ring underrun
        while (filled < nChunkSize)
            pBuffer[filled++] = 0;
        return nChunkSize;
    }
};

// ---- Module state -------------------------------------------------------

static CircleVCHIQSound *s_pSound     = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;
static CVCHIQDevice     *s_pVCHIQ     = nullptr;
static bool              s_active     = false;

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

extern "C" void circle_audio_set_vchiq(void *pVCHIQ)
{
    s_pVCHIQ = (CVCHIQDevice *)pVCHIQ;
}

// Called from kernel.cpp on core 0
extern "C" void circle_audio_init_device(void)
{
    if (!s_pVCHIQ || s_pSound) return;

    constexpr unsigned SAMPLE_RATE = 44100;
    constexpr unsigned CHUNK_SIZE  = 1024;  // like bmc64

    s_pSound = new CircleVCHIQSound(s_pVCHIQ, SAMPLE_RATE, CHUNK_SIZE,
                                     VCHIQSoundDestinationAuto);

    if (!s_pSound->Start())
    {
        delete s_pSound;
        s_pSound = nullptr;
    }
}

// ---- Audio API ----------------------------------------------------------

bool Audio::Init()
{
    if (s_pSound)
        s_active = true;
    return true;
}

void Audio::Exit()
{
    if (s_pSound)
    {
        s_pSound->Cancel();
        delete s_pSound;
        s_pSound = nullptr;
    }
    s_active = false;
    s_ring_head = s_ring_tail = 0;
}

// Called every frame with stereo int16 PCM.
// Busy-waits when ring buffer full — VCHIQ DMA drains at 44100Hz.
float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!s_active || !pData || len_bytes <= 0)
        return 0.5f;

    const s16 *src = reinterpret_cast<const s16 *>(pData);
    int n = len_bytes / sizeof(s16);

    int written = 0;
    while (written < n)
    {
        asm volatile("dmb" ::: "memory");
        unsigned free = (RING_SAMPLES - ring_used() - 1);
        if (free >= 2)
        {
            unsigned h = s_ring_head;
            s_ring[h] = src[written++]; h = (h + 1) % RING_SAMPLES;
            s_ring[h] = src[written++]; h = (h + 1) % RING_SAMPLES;
            asm volatile("dmb" ::: "memory");
            s_ring_head = h;
        }
        else
        {
            // Ring full — VCHIQ on core 0 drains it
            asm volatile("nop");
        }
    }

    return 0.5f;
}
