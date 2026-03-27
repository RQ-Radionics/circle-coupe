// VCHIQSound.h — VCHIQ audio device for SimCoupe (based on BMC64's approach)
// Push-based: audio data is sent directly to VC4, no CSoundBaseDevice queue.
#pragma once

#include <circle/types.h>
#include <circle/sched/synchronizationevent.h>
#include <vc4/vchiq/vchiqdevice.h>
#include <vc4/vchi/vchi.h>
#include <vc4/sound/vc_vchi_audioserv_defs.h>

enum TVCHIQSoundDestination {
    VCHIQSoundDestinationAuto,
    VCHIQSoundDestinationHeadphones,
    VCHIQSoundDestinationHDMI,
    VCHIQSoundDestinationUnknown
};

enum TVCHIQSoundState {
    VCHIQSoundCreated,
    VCHIQSoundIdle,
    VCHIQSoundRunning,
    VCHIQSoundCancelled,
    VCHIQSoundTerminating,
    VCHIQSoundError
};

class VCHIQSound {
public:
    VCHIQSound(CVCHIQDevice *pVCHIQDevice,
               unsigned nSampleRate,
               unsigned nChunkSize,
               TVCHIQSoundDestination Destination);
    ~VCHIQSound();

    boolean Start();
    void Cancel();
    boolean IsActive() const { return m_State >= VCHIQSoundRunning; }
    int GetState() const { return (int)m_State; }
    unsigned GetWritePos() const { return m_nWritePos; }
    unsigned GetCompletePos() const { return m_nCompletePos; }

    // Push audio data directly to VC4 (called from any core)
    int WriteSamples(const s16 *pBuffer, unsigned nSamples);

private:
    int WriteChunk(const s16 *pData, unsigned nSamples);
    int CallMessage(VC_AUDIO_MSG_T *pMessage);
    int QueueMessage(VC_AUDIO_MSG_T *pMessage);

    void Callback(const VCHI_CALLBACK_REASON_T Reason, void *hMessage);
    static void CallbackStub(void *pParam, const VCHI_CALLBACK_REASON_T Reason,
                              void *hMessage);

    unsigned m_nSampleRate;
    unsigned m_nChunkSize;
    TVCHIQSoundDestination m_Destination;
    volatile TVCHIQSoundState m_State;

    VCHI_INSTANCE_T m_VCHIInstance;
    VCHI_SERVICE_HANDLE_T m_hService;

    CSynchronizationEvent m_Event;
    int m_nResult;

    unsigned m_nWritePos;
    unsigned m_nCompletePos;
};
