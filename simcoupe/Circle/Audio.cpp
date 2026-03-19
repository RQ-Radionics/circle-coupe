// Audio.cpp — PWM audio with GetChunk + ring buffer
//
// Core 0 DMA: calls GetChunk() which reads from ring buffer
// Core 2: Sound::FrameUpdate() → Audio::AddData() writes to ring buffer
// No Write() call — ring buffer is our own lock-free SPSC queue.

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>

// ---- Ring buffer (Core 2 writes, Core 0 DMA reads) ----

static constexpr unsigned RING_FRAMES  = 8192;
static constexpr unsigned RING_CH      = 2;
static constexpr unsigned RING_SAMPLES = RING_FRAMES * RING_CH;

static s16      s_ring[RING_SAMPLES];
static volatile unsigned s_ring_head = 0;   // written by Core 2
static volatile unsigned s_ring_tail = 0;   // written by Core 0 DMA

static inline unsigned ring_used() {
    unsigned h = s_ring_head, t = s_ring_tail;
    return (h - t + RING_SAMPLES) % RING_SAMPLES;
}

// ---- PWM with GetChunk override ----

class CirclePWM : public CPWMSoundBaseDevice
{
public:
    CirclePWM(CInterruptSystem *pInt, unsigned rate, unsigned chunk)
    : CPWMSoundBaseDevice(pInt, rate, chunk) {}

protected:
    // Called by DMA IRQ on Core 0
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override
    {
        for (unsigned i = 0; i < nChunkSize; i++)
        {
            s16 l = 0, r = 0;
            asm volatile("dmb" ::: "memory");  // see Core 2's s_ring_head
            if (ring_used() >= 2)
            {
                unsigned t = s_ring_tail;
                l = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                r = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                asm volatile("dmb" ::: "memory");  // publish new s_ring_tail
                s_ring_tail = t;
            }
            u32 ul = (u32)((u16)(l + 32768));
            u32 ur = (u32)((u16)(r + 32768));
            pBuffer[i] = (ur << 16) | ul;
        }
        return nChunkSize;
    }
};

// ---- State ----

static CirclePWM       *s_pSound     = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;

const char *g_audio_status = "no-init";

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

extern "C" void circle_audio_set_vchiq(void *) {}
// Create and start device on Core 0
extern "C" void circle_audio_init_device(void)
{
    if (!s_pInterrupt || s_pSound) return;

    g_audio_status = "creating";
    s_pSound = new CirclePWM(s_pInterrupt, 44100, 2048);

    g_audio_status = "starting";
    if (!s_pSound->Start()) {
        g_audio_status = "start-fail";
        delete s_pSound;
        s_pSound = nullptr;
        return;
    }
    g_audio_status = "running";
}

extern "C" void circle_audio_set_device(void *) {}
extern "C" void circle_audio_start(void) {}

// Called from Core 2 — refresh s_pSound pointer for this core's cache
extern "C" void circle_audio_activate(void *)
{
    // s_pSound was set on Core 0, may not be visible here.
    // Nothing to do — AddData no longer checks s_active,
    // and s_pSound is read with volatile qualifier.
}

// ---- Audio API ----

bool Audio::Init()
{
    return true;
}

void Audio::Exit()
{
    s_ring_head = s_ring_tail = 0;
}

float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!pData || len_bytes <= 0)
        return 0.5f;

    const s16 *src = reinterpret_cast<const s16 *>(pData);
    int n = len_bytes / sizeof(s16);

    for (int i = 0; i < n; i += 2)
    {
        asm volatile("dmb" ::: "memory");  // see Core 0's s_ring_tail
        unsigned free = RING_SAMPLES - ring_used() - 1;
        if (free < 2) break;

        unsigned h = s_ring_head;
        s_ring[h] = src[i];     h = (h + 1) % RING_SAMPLES;
        s_ring[h] = src[i + 1]; h = (h + 1) % RING_SAMPLES;
        asm volatile("dmb" ::: "memory");  // publish new s_ring_head
        s_ring_head = h;
    }

    return 0.5f;
}
