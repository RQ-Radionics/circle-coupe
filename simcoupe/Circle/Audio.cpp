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

// Ring buffer for Core 2 → Core 0 audio transfer (VCHIQ needs Core 0's scheduler)
#define AUDIO_RING_SIZE (22050 * 2 * 2)  // ~1s stereo s16
static s16  s_ring[AUDIO_RING_SIZE];
static volatile unsigned s_ring_wr = 0;
static volatile unsigned s_ring_rd = 0;

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
        // Store initial state — poll will show ongoing state
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf,
                 "init:st%d", s_pVCHIQ->GetState());
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

// Called from Core 0 main loop — drains ring buffer to VCHIQ
static volatile unsigned s_poll_count = 0;
static volatile int s_poll_last_sent = -1;

extern "C" void circle_audio_poll(void)
{
    if (!s_pVCHIQ) return;
    s_poll_count++;

    unsigned rd = s_ring_rd;
    unsigned wr = s_ring_wr;
    asm volatile("dmb" ::: "memory");

    if (rd == wr) return;  // empty

    unsigned avail = (wr >= rd) ? (wr - rd) : (AUDIO_RING_SIZE - rd + wr);
    if (avail > 2048) avail = 2048;

    s16 tmp[2048];
    for (unsigned i = 0; i < avail; i++) {
        tmp[i] = s_ring[(rd + i) % AUDIO_RING_SIZE];
    }

    int sent = s_pVCHIQ->WriteSamples(tmp, avail);
    s_poll_last_sent = sent;
    if (sent > 0) {
        asm volatile("dmb" ::: "memory");
        s_ring_rd = (rd + sent) % AUDIO_RING_SIZE;
    }

    if ((s_poll_count & 0x3F) == 1)
        snprintf(g_audio_status_buf, sizeof g_audio_status_buf,
                 "st%d s%d w%u c%u", s_pVCHIQ->GetState(), sent,
                 s_pVCHIQ->GetWritePos(), s_pVCHIQ->GetCompletePos());
}

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
        // Write to ring buffer — Core 0 drains to VCHIQ via circle_audio_poll()
        const s16 *pSamples = (const s16 *)pData;
        unsigned nSamples = len_bytes / sizeof(s16);
        unsigned wr = s_ring_wr;
        unsigned rd = s_ring_rd;
        unsigned free = (rd > wr) ? (rd - wr - 1) : (AUDIO_RING_SIZE - wr + rd - 1);
        if (nSamples > free) nSamples = free;  // drop excess

        for (unsigned i = 0; i < nSamples; i++)
            s_ring[(wr + i) % AUDIO_RING_SIZE] = pSamples[i];

        asm volatile("dmb" ::: "memory");
        s_ring_wr = (wr + nSamples) % AUDIO_RING_SIZE;

        written = nSamples * sizeof(s16);
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
