// Audio.cpp — PWM audio: ring buffer (Core 2) + Write (Core 0)
//
// Core 2: Sound::FrameUpdate() → AddData() → ring buffer push
// Core 0: circle_audio_poll() → ring buffer → Write() to device
// Write() only called from Core 0 = same core as DMA = thread-safe

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>

// ---- Ring buffer: Core 2 writes bytes, Core 0 reads bytes ----

static constexpr unsigned RING_SIZE = 32768;
static uint8_t s_ring[RING_SIZE];
static volatile unsigned s_ring_head = 0;
static volatile unsigned s_ring_tail = 0;

static inline unsigned ring_used() {
    return (s_ring_head - s_ring_tail + RING_SIZE) % RING_SIZE;
}
static inline unsigned ring_free() {
    return RING_SIZE - ring_used() - 1;
}

// ---- State ----

static CPWMSoundBaseDevice * volatile s_pSound = nullptr;
static CInterruptSystem *s_pInterrupt = nullptr;

const char *g_audio_status = "no-init";

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

extern "C" void circle_audio_set_vchiq(void *) {}

extern "C" void circle_audio_set_device(void *pDev)
{
    s_pSound = (CPWMSoundBaseDevice *)pDev;
}

extern "C" void circle_audio_start(void)
{
    if (!s_pSound) return;

    g_audio_status = "alloc";
    if (!s_pSound->AllocateQueue(200)) {
        g_audio_status = "alloc-fail";
        s_pSound = nullptr;
        return;
    }

    s_pSound->SetWriteFormat(SoundFormatSigned16, 2);

    unsigned nFrames = s_pSound->GetQueueSizeFrames();
    unsigned nBytes = nFrames * 2 * sizeof(s16);
    s16 *sil = new s16[nFrames * 2];
    for (unsigned i = 0; i < nFrames * 2; i++) sil[i] = 0;
    s_pSound->Write(sil, nBytes);
    delete[] sil;

    g_audio_status = "starting";
    if (!s_pSound->Start()) {
        g_audio_status = "start-fail";
        s_pSound = nullptr;
        return;
    }
    g_audio_status = "running";
}

extern "C" void circle_audio_init_device(void) {}
extern "C" void circle_audio_activate(void *) {}

// Called from Core 0 scheduler loop — drains ring to device via Write()
extern "C" void circle_audio_poll(void)
{
    if (!s_pSound) return;

    asm volatile("dmb" ::: "memory");
    unsigned used = ring_used();
    if (used < 4) return;  // need at least 1 stereo frame (4 bytes)

    // Align to 4 bytes (one stereo s16 frame)
    used &= ~3u;
    if (used > 4096) used = 4096;

    unsigned t = s_ring_tail;
    unsigned end = (t + used) % RING_SIZE;

    if (end > t) {
        s_pSound->Write(&s_ring[t], used);
    } else {
        unsigned first = RING_SIZE - t;
        s_pSound->Write(&s_ring[t], first);
        if (end > 0)
            s_pSound->Write(&s_ring[0], end);
    }

    asm volatile("dmb" ::: "memory");
    s_ring_tail = (t + used) % RING_SIZE;
}

// ---- Audio API ----

bool Audio::Init() { return true; }

void Audio::Exit() { s_ring_head = s_ring_tail = 0; }

// Called from Core 2 — push bytes to ring buffer
float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!pData || len_bytes <= 0)
        return 0.5f;

    unsigned to_write = (unsigned)len_bytes;
    unsigned free = ring_free();
    if (to_write > free) to_write = free;

    unsigned h = s_ring_head;
    unsigned end = (h + to_write) % RING_SIZE;
    if (end > h) {
        memcpy(&s_ring[h], pData, to_write);
    } else {
        unsigned first = RING_SIZE - h;
        memcpy(&s_ring[h], pData, first);
        memcpy(&s_ring[0], pData + first, end);
    }
    asm volatile("dmb" ::: "memory");
    s_ring_head = end;

    return 0.5f;
}
