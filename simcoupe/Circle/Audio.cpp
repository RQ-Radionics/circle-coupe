// Audio.cpp — PWM audio with device owned by kernel (constructed before multicore)

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>

static CPWMSoundBaseDevice *s_pSound     = nullptr;
static CInterruptSystem    *s_pInterrupt = nullptr;
static bool                 s_active     = false;

const char *g_audio_status = "no-init";

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

// Device is owned by CKernel — just store the pointer
extern "C" void circle_audio_set_device(void *pDev)
{
    s_pSound = (CPWMSoundBaseDevice *)pDev;
    g_audio_status = "device-set";
}

extern "C" void circle_audio_set_vchiq(void *) {}

// Called from Run() on Core 0 — AllocateQueue + Start like sample 34
extern "C" void circle_audio_start(void)
{
    if (!s_pSound) return;

    g_audio_status = "alloc-queue";
    if (!s_pSound->AllocateQueue(100)) {
        g_audio_status = "alloc-fail";
        s_pSound = nullptr;
        return;
    }

    s_pSound->SetWriteFormat(SoundFormatSigned16, 2);

    // Fill queue with silence before Start (like sample 34)
    unsigned nFrames = s_pSound->GetQueueSizeFrames();
    s16 *sil = new s16[nFrames * 2];
    for (unsigned i = 0; i < nFrames * 2; i++) sil[i] = 0;
    s_pSound->Write(sil, nFrames * 2 * sizeof(s16));
    delete[] sil;

    g_audio_status = "starting";
    if (!s_pSound->Start()) {
        g_audio_status = "start-fail";
        s_pSound = nullptr;
        return;
    }

    s_active = true;
    g_audio_status = "running";
}

// Also keep old entry point as no-op
extern "C" void circle_audio_init_device(void) {}

bool Audio::Init()
{
    return true;
}

void Audio::Exit()
{
    // Don't delete — kernel owns the device
    s_active = false;
}

float Audio::AddData(uint8_t *pData, int len_bytes)
{
    if (!s_active || !pData || len_bytes <= 0 || !s_pSound)
        return 0.5f;
    s_pSound->Write(pData, len_bytes);
    return 0.5f;
}
