// Audio.cpp — PWM audio with device owned by kernel (constructed before multicore)

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>

static CPWMSoundBaseDevice * volatile s_pSound     = nullptr;
static CInterruptSystem    *s_pInterrupt = nullptr;
static volatile bool        s_active     = false;

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

    // s_active set from Core 2 via circle_audio_activate() — cross-core cache issue
    g_audio_status = "running";
}

// Also keep old entry point as no-op
extern "C" void circle_audio_init_device(void) {}

// Called from Core 2 with the device pointer — avoids cross-core cache issue
extern "C" void circle_audio_activate(void *pDevice)
{
    s_pSound = (CPWMSoundBaseDevice *)pDevice;
    s_active = true;
}

bool Audio::Init()
{
    return true;
}

void Audio::Exit()
{
    // Don't delete — kernel owns the device
    s_active = false;
}

static volatile unsigned s_add_count = 0;
static volatile unsigned s_add_bytes = 0;
static volatile unsigned s_add_called = 0;
static volatile int s_add_last_len = 0;
static volatile int s_add_reject_reason = 0;  // 1=!active 2=!pData 3=len<=0 4=!dev

float Audio::AddData(uint8_t *pData, int len_bytes)
{
    s_add_called++;
    s_add_last_len = len_bytes;

    // Get device pointer with memory barrier to ensure cross-core visibility
    asm volatile("dmb" ::: "memory");
    CPWMSoundBaseDevice *dev = s_pSound;
    bool active = s_active;

    // Skip s_active check — just verify we have data and a device
    if (!pData)        { s_add_reject_reason = 2; return 0.5f; }
    if (len_bytes <= 0){ s_add_reject_reason = 3; return 0.5f; }
    if (!dev)          { s_add_reject_reason = 4; return 0.5f; }
    int written = dev->Write(pData, len_bytes);
    s_add_count++;
    s_add_bytes += written;
    return 0.5f;
}

extern "C" unsigned circle_audio_add_called(void) { return s_add_called; }
extern "C" int circle_audio_last_len(void) { return s_add_last_len; }
extern "C" int circle_audio_reject(void) { return s_add_reject_reason; }

// Expose for profiling
extern "C" unsigned circle_audio_add_count(void) { return s_add_count; }
extern "C" unsigned circle_audio_add_bytes(void) { return s_add_bytes; }
