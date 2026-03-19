// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: Circle bare-metal audio via PWM (jack 3.5mm)
//
// Uses the standard Circle AllocateQueue+Write model (sample 34).
// NO GetChunk override — the DMA pulls from Circle's internal queue.
// Sound::FrameUpdate() on Core 2 calls Audio::AddData() which writes
// to the queue via CSoundBaseDevice::Write().

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

// ---- Module state -------------------------------------------------------

static CPWMSoundBaseDevice *s_pSound     = nullptr;
static CInterruptSystem    *s_pInterrupt = nullptr;
static bool                 s_active     = false;
static bool                 s_started    = false;

const char *g_audio_status = "no-init";

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// Unused VCHIQ stub
extern "C" void circle_audio_set_vchiq(void *) {}

// Called from kernel.cpp Run() on Core 0 — AFTER all initialization
extern "C" void circle_audio_init_device(void)
{
    if (!s_pInterrupt || s_pSound) return;

    g_audio_status = "creating";

    // Create device exactly like Circle sample 34
    s_pSound = new CPWMSoundBaseDevice(s_pInterrupt, 44100, 2048);

    // Allocate queue — MUST be before SetWriteFormat and Start
    if (!s_pSound->AllocateQueue(100))  // 100ms queue
    {
        g_audio_status = "alloc-fail";
        delete s_pSound;
        s_pSound = nullptr;
        return;
    }

    s_pSound->SetWriteFormat(SoundFormatSigned16, 2);  // stereo 16-bit

    // Fill queue with silence before Start (like sample 34)
    unsigned nQueueFrames = s_pSound->GetQueueSizeFrames();
    unsigned nBytes = nQueueFrames * 2 * sizeof(s16);  // stereo
    s16 *silence = new s16[nQueueFrames * 2];
    for (unsigned i = 0; i < nQueueFrames * 2; i++) silence[i] = 0;
    s_pSound->Write(silence, nBytes);
    delete[] silence;

    g_audio_status = "starting";

    if (!s_pSound->Start())
    {
        g_audio_status = "start-fail";
        delete s_pSound;
        s_pSound = nullptr;
        return;
    }

    s_started = true;
    g_audio_status = "running";
}

// ---- Audio API ----------------------------------------------------------

bool Audio::Init()
{
    if (s_started)
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
    s_started = false;
}

// Called from Sound::FrameUpdate() on Core 2 with stereo int16 PCM.
// Write() to Circle's internal queue. Non-blocking: drops if queue full.
float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!s_active || !pData || len_bytes <= 0 || !s_pSound)
        return 0.5f;

    // Write to Circle queue — returns bytes written, may be less if full
    s_pSound->Write(pData, len_bytes);

    return 0.5f;
}
