// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio via PWM (jack 3.5mm) or HDMI
//
// Uses GetChunk() pull model (like bmc64):
//   Sound::FrameUpdate() → Audio::AddData() → ring buffer (push)
//   DMA IRQ → GetChunk() → ring buffer (pull)
//
// The DMA running at 44100Hz is the natural master clock for emulation.
// When the ring buffer is full, AddData() busy-waits — this backpressure
// regulates the Z80 to real-time speed.

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Ring buffer (lock-free: core 1 writes, core 0 DMA reads) -----------

static constexpr unsigned RING_FRAMES  = 4096;   // ~93ms at 44100 Hz
static constexpr unsigned RING_CH      = 2;       // stereo
static constexpr unsigned RING_SAMPLES = RING_FRAMES * RING_CH;

static int16_t  s_ring[RING_SAMPLES];
static volatile unsigned s_ring_head = 0;   // write index (core 1, emulator)
static volatile unsigned s_ring_tail = 0;   // read  index (core 0, DMA IRQ)

static inline unsigned ring_used() {
    return (s_ring_head - s_ring_tail + RING_SAMPLES) % RING_SAMPLES;
}
static inline unsigned ring_free() {
    return RING_SAMPLES - ring_used() - 1;
}

// Pull one stereo sample pair from ring buffer, or silence if empty
static inline void ring_pull(int16_t &l, int16_t &r)
{
    asm volatile("dmb" ::: "memory");
    if (ring_used() >= 2) {
        unsigned t = s_ring_tail;
        l = s_ring[t]; t = (t + 1) % RING_SAMPLES;
        r = s_ring[t]; t = (t + 1) % RING_SAMPLES;
        asm volatile("dmb" ::: "memory");
        s_ring_tail = t;
    } else {
        l = r = 0;  // silence on underrun
    }
}

// ---- Sound device with GetChunk override --------------------------------

class CircleSound : public CPWMSoundBaseDevice
{
public:
    CircleSound(CInterruptSystem *pInt, unsigned nSampleRate, unsigned nChunk)
    : CPWMSoundBaseDevice(pInt, nSampleRate, nChunk) {}

protected:
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override
    {
        for (unsigned i = 0; i < nChunkSize; i++)
        {
            int16_t l, r;
            ring_pull(l, r);
            // PWM expects unsigned 16-bit per channel, centre=0x8000
            uint16_t ul = (uint16_t)(l + 32768);
            uint16_t ur = (uint16_t)(r + 32768);
            pBuffer[i] = ((u32)ur << 16) | ul;
        }
        return nChunkSize;
    }
};

// ---- Module state -------------------------------------------------------

static CircleSound      *s_pSound     = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;
static bool              s_active     = false;

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// ---- Audio API ----------------------------------------------------------

bool Audio::Init()
{
    Exit();

    if (!s_pInterrupt)
        return false;

    constexpr unsigned SAMPLE_RATE = 44100;
    constexpr unsigned CHUNK_SIZE  = 512;   // smaller = less latency

    s_pSound = new CircleSound(s_pInterrupt, SAMPLE_RATE, CHUNK_SIZE);

    // GetChunk model: NO AllocateQueue, NO SetWriteFormat.
    // Just Start() — DMA will call our GetChunk() to pull samples.

    if (!s_pSound->Start())
    {
        delete s_pSound;
        s_pSound = nullptr;
        // Non-fatal: emulator runs without sound
        return true;
    }

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
// When ring buffer is full, busy-waits — DMA drains at 44100Hz.
// This is the master clock for emulation speed.
float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!s_active || !pData || len_bytes <= 0)
        return 0.5f;

    const int16_t *src = reinterpret_cast<const int16_t *>(pData);
    int n = len_bytes / sizeof(int16_t);

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
            // Ring full — DMA on core 0 will drain it.
            // Brief nop to avoid hammering the bus.
            asm volatile("nop");
        }
    }

    // Return 0.5 — Sound.cpp must NOT do extra sleep
    return 0.5f;
}
