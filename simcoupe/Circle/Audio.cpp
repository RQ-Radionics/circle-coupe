// Audio.cpp — Sound output: PWM via CSoundBaseDevice, HDMI via VCHIQSound

#include "SimCoupe.h"
#include "Audio.h"
#include "Sound.h"
#include "Options.h"

#include <circle/sound/soundbasedevice.h>
#include "VCHIQSound.h"
#include <circle/interrupt.h>
#include <stdio.h>

static CSoundBaseDevice  * volatile s_pPWM   = nullptr;
static VCHIQSound        * volatile s_pVCHIQ = nullptr;
static CInterruptSystem  *s_pInterrupt = nullptr;
static volatile bool      s_active     = false;

char g_audio_status_buf[64] = "no-init";
const char *g_audio_status = g_audio_status_buf;

extern "C" void circle_audio_set_interrupt(void *pInt)
{
    s_pInterrupt = (CInterruptSystem *)pInt;
}

extern "C" void circle_audio_set_device(void *pDev)
{
    s_pPWM = (CSoundBaseDevice *)pDev;
    snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "pwm-set");
}

extern "C" void circle_audio_set_vchiq_device(void *pDev)
{
    s_pVCHIQ = (VCHIQSound *)pDev;
    snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "vchiq-set");
}

extern "C" void circle_audio_set_hdmi_polling(void *) {}
extern "C" void circle_audio_set_vchiq(void *) {}

// Called from Run() on Core 0
extern "C" void circle_audio_start(void)
{
    // VCHIQ path
    if (s_pVCHIQ) {
        if (!s_pVCHIQ->Start()) {
            snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "VCHIQ-FAIL");
            s_pVCHIQ = nullptr;
            return;
        }
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "vchiq-ok");
        return;
    }

    // PWM path
    if (!s_pPWM) return;

    if (!s_pPWM->AllocateQueue(1000)) {
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "alloc-fail");
        s_pPWM = nullptr;
        return;
    }

    s_pPWM->SetWriteFormat(SoundFormatSigned16, 2);

    unsigned nFrames = s_pPWM->GetQueueSizeFrames();
    s16 *sil = new s16[nFrames * 2];
    for (unsigned i = 0; i < nFrames * 2; i++) sil[i] = 0;
    s_pPWM->Write(sil, nFrames * 2 * sizeof(s16));
    delete[] sil;

    if (!s_pPWM->Start()) {
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "start-fail");
        s_pPWM = nullptr;
        return;
    }

    snprintf(g_audio_status_buf, sizeof g_audio_status_buf, "pwm-ok");
}

extern "C" void circle_audio_init_device(void) {}

extern "C" void circle_audio_activate(void *pDevice)
{
    if (!s_pVCHIQ)
        s_pPWM = (CSoundBaseDevice *)pDevice;
    s_active = true;
}

bool Audio::Init()
{
    return true;
}

void Audio::Exit()
{
    if (s_pVCHIQ)
        s_pVCHIQ->Cancel();
    else if (s_pPWM)
        s_pPWM->Cancel();
    s_active = false;
}

static volatile unsigned s_add_count = 0;
static volatile unsigned s_add_bytes = 0;
static volatile unsigned s_add_called = 0;
static volatile int s_add_last_len = 0;
static volatile int s_add_reject_reason = 0;

float Audio::AddData(uint8_t *pData, int len_bytes)
{
    s_add_called++;
    s_add_last_len = len_bytes;

    asm volatile("dmb" ::: "memory");

    if (!pData)        { s_add_reject_reason = 2; return 0.5f; }
    if (len_bytes <= 0){ s_add_reject_reason = 3; return 0.5f; }

    int written = 0;

    if (s_pVCHIQ) {
        unsigned nSamples = len_bytes / sizeof(s16);
        written = s_pVCHIQ->WriteSamples((const s16 *)pData, nSamples);
        written *= sizeof(s16);
    } else if (s_pPWM) {
        written = s_pPWM->Write(pData, len_bytes);
    } else {
        s_add_reject_reason = 4;
        return 0.5f;
    }

    s_add_count++;
    s_add_bytes += written;

    if ((s_add_count & 0xFF) == 0)
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf,
                 "w%u=%d", s_add_count, written);

    return 0.5f;
}

extern "C" unsigned circle_audio_add_called(void) { return s_add_called; }
extern "C" int circle_audio_last_len(void) { return s_add_last_len; }
extern "C" int circle_audio_reject(void) { return s_add_reject_reason; }
extern "C" unsigned circle_audio_add_count(void) { return s_add_count; }
extern "C" unsigned circle_audio_add_bytes(void) { return s_add_bytes; }
