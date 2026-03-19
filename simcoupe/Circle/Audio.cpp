// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio via HDMI
//
// Uses CSoundBaseDevice Write() model with AllocateQueue:
//   Sound::FrameUpdate() → Audio::AddData() → Write() to sound queue
//   DMA IRQ drains the queue at 44100Hz
//
// AddData() blocks via Write() when queue is full — this is the
// natural master clock for emulation speed (like bmc64).

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Module state -------------------------------------------------------

static CHDMISoundBaseDevice *s_pSound     = nullptr;
static CInterruptSystem     *s_pInterrupt = nullptr;
static bool                  s_active     = false;

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// Called from kernel.cpp on core 0 — DMA IRQs must be on core 0
extern "C" void circle_audio_init_device(void)
{
    if (!s_pInterrupt || s_pSound) return;

    constexpr unsigned SAMPLE_RATE = 44100;
    constexpr unsigned CHUNK_SIZE  = 384 * 2;  // must be multiple of IEC958_SUBFRAMES_PER_BLOCK (384)

    s_pSound = new CHDMISoundBaseDevice(s_pInterrupt, SAMPLE_RATE, CHUNK_SIZE);

    if (!s_pSound->AllocateQueue(100))  // 100ms queue
    {
        delete s_pSound;
        s_pSound = nullptr;
        return;
    }

    s_pSound->SetWriteFormat(SoundFormatSigned16, 2);

    if (!s_pSound->Start())
    {
        delete s_pSound;
        s_pSound = nullptr;
    }
}

// ---- Audio API ----------------------------------------------------------

bool Audio::Init()
{
    // Device already created on core 0 via circle_audio_init_device()
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
}

// Called every frame with stereo int16 PCM.
// Write() blocks when queue is full — DMA drains at 44100Hz.
// This is the master clock for emulation speed.
float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!s_active || !pData || len_bytes <= 0)
        return 0.5f;

    // Write blocks until there's room in the queue.
    // This is exactly how bmc64 regulates emulation speed.
    int written = s_pSound->Write(pData, len_bytes);

    // Return 0.5 — Sound.cpp must NOT add extra sleep
    return 0.5f;
}
