// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio via PWM (jack 3.5mm)
//
// GetChunk pull model: DMA IRQ on Core 0 calls GetChunk() which reads
// from a ring buffer. Sound::FrameUpdate() on Core 2 pushes samples
// via Audio::AddData(). Core 1 (Z80) is not involved.

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Ring buffer (lock-free: Core 2 writes, Core 0 DMA reads) -----------

static constexpr unsigned RING_FRAMES  = 4096;
static constexpr unsigned RING_CH      = 2;
static constexpr unsigned RING_SAMPLES = RING_FRAMES * RING_CH;

static s16      s_ring[RING_SAMPLES];
static volatile unsigned s_ring_head = 0;
static volatile unsigned s_ring_tail = 0;

static inline unsigned ring_used() {
    return (s_ring_head - s_ring_tail + RING_SAMPLES) % RING_SAMPLES;
}
static inline unsigned ring_free() {
    return RING_SAMPLES - ring_used() - 1;
}

// ---- PWM sound device with GetChunk override ----------------------------

class CirclePWMSound : public CPWMSoundBaseDevice
{
public:
    CirclePWMSound(CInterruptSystem *pInt, unsigned nSampleRate, unsigned nChunk)
    : CPWMSoundBaseDevice(pInt, nSampleRate, nChunk) {}

protected:
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override
    {
        for (unsigned i = 0; i < nChunkSize; i++)
        {
            s16 l = 0, r = 0;
            asm volatile("dmb" ::: "memory");
            if (ring_used() >= 2)
            {
                unsigned t = s_ring_tail;
                l = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                r = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                asm volatile("dmb" ::: "memory");
                s_ring_tail = t;
            }
            // PWM: unsigned 16-bit per channel, centre=0x8000
            u32 ul = (u32)((u16)(l + 32768));
            u32 ur = (u32)((u16)(r + 32768));
            pBuffer[i] = (ur << 16) | ul;
        }
        return nChunkSize;
    }
};

// ---- Module state -------------------------------------------------------

static CirclePWMSound   *s_pSound     = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;
static bool              s_active     = false;

const char *g_audio_status = "no-init";

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// Unused VCHIQ stubs (kept for link compatibility)
extern "C" void circle_audio_set_vchiq(void *) {}

// Called from kernel.cpp on Core 0 — DMA IRQs must be on Core 0
extern "C" void circle_audio_init_device(void)
{
    if (!s_pInterrupt || s_pSound) return;

    constexpr unsigned SAMPLE_RATE = 44100;
    constexpr unsigned CHUNK_SIZE  = 512;

    g_audio_status = "creating";
    s_pSound = new CirclePWMSound(s_pInterrupt, SAMPLE_RATE, CHUNK_SIZE);

    g_audio_status = "starting";
    if (!s_pSound->Start())
    {
        g_audio_status = "start-fail";
        delete s_pSound;
        s_pSound = nullptr;
        return;
    }

    g_audio_status = "running";
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
        if (ring_free() >= 2)
        {
            unsigned h = s_ring_head;
            s_ring[h] = src[written++]; h = (h + 1) % RING_SAMPLES;
            s_ring[h] = src[written++]; h = (h + 1) % RING_SAMPLES;
            asm volatile("dmb" ::: "memory");
            s_ring_head = h;
        }
        else
        {
            // Ring full — drop remaining samples
            break;
        }
    }

    return 0.5f;
}
