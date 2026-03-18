// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio output
//
// Architecture:
//   Audio::AddData() → ring buffer (push, called from emulator core 1)
//   CPWMSoundBaseDevice::GetChunk() ← ring buffer (pull, DMA IRQ on core 0)
//
// The DMA pull rate (44100 Hz) is the master clock for emulation speed.
// When the ring buffer is full, AddData() spins briefly — this is the
// backpressure that regulates the Z80 emulation to real time.
//
// PWM = 3.5mm jack. HDMI audio (VCHIQ) added in circle-coupe-k4c.

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Ring buffer (stereo int16, shared between core 0 DMA and core 1 emu) --

static constexpr unsigned RING_FRAMES  = 4096;   // ~93ms at 44100 Hz
static constexpr unsigned RING_CH      = 2;
static constexpr unsigned RING_SAMPLES = RING_FRAMES * RING_CH;

static int16_t  s_ring[RING_SAMPLES];
static volatile unsigned s_ring_head = 0;   // write index (core 1)
static volatile unsigned s_ring_tail = 0;   // read  index (core 0 DMA)

static inline unsigned ring_used() {
    return (s_ring_head - s_ring_tail + RING_SAMPLES) % RING_SAMPLES;
}
static inline unsigned ring_free() {
    return RING_SAMPLES - ring_used() - 1;
}

// ---- PWM sound device ---------------------------------------------------

class CirclePWMSound : public CPWMSoundBaseDevice
{
public:
    CirclePWMSound(CInterruptSystem *pInt, unsigned nSampleRate, unsigned nChunk)
    : CPWMSoundBaseDevice(pInt, nSampleRate, nChunk) {}

protected:
    // Called by DMA IRQ to fill the next DMA chunk.
    // Pull samples from the ring buffer; output silence if starved.
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override
    {
        // nChunkSize is in u32 words; each word = one stereo sample pair
        // Circle PWM expects: bits 31:16 = right channel, bits 15:0 = left
        // Range: 0x0000–0xFFFF (unsigned 16-bit), centre = 0x8000
        for (unsigned i = 0; i < nChunkSize; i++)
        {
            int16_t l = 0, r = 0;
            asm volatile("dmb" ::: "memory");
            if (ring_used() >= 2)
            {
                unsigned t = s_ring_tail;
                l = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                r = s_ring[t]; t = (t + 1) % RING_SAMPLES;
                asm volatile("dmb" ::: "memory");
                s_ring_tail = t;
            }
            // Convert signed int16 → unsigned 16-bit for PWM
            uint16_t ul = (uint16_t)(l + 32768);
            uint16_t ur = (uint16_t)(r + 32768);
            pBuffer[i] = ((u32)ur << 16) | ul;
        }
        return nChunkSize;
    }
};

// ---- Module state -------------------------------------------------------

static CirclePWMSound   *s_pPWM       = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;
static bool              s_active     = false;

// Called from kernel.cpp before SDL_Init (before Audio::Init)
extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// ---- Audio API ----------------------------------------------------------

bool Audio::Init()
{
    // Audio disabled temporarily — PWM DMA Start() may block indefinitely
    // TODO: fix CPWMSoundBaseDevice init (circle-coupe-k4c)
    s_active = false;
    return true;
}

void Audio::Exit()
{
    if (s_pPWM)
    {
        s_pPWM->Cancel();
        delete s_pPWM;
        s_pPWM = nullptr;
    }
    s_active = false;
    s_ring_head = s_ring_tail = 0;
}

// Called every frame with stereo int16 PCM.
// Blocks briefly if the ring buffer is full (natural throttle at 44100 Hz).
// Returns fill ratio [0,1] so Sound.cpp can do minor speed adjustments.
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
            // Ring full — yield briefly (DMA IRQ on core 0 will drain it)
            CTimer::Get()->usDelay(100);
        }
    }

    // Return fill ratio so Sound.cpp speed-adjust keeps ~50% buffer level
    return (float)ring_used() / (float)(RING_SAMPLES - 1);
}
