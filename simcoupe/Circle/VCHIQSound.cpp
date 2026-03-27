// VCHIQSound.cpp — VCHIQ push-based audio (adapted from BMC64)
#include "VCHIQSound.h"
#include <circle/devicenameservice.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <assert.h>
#include <string.h>

#define VOLUME_TO_CHIP(volume) ((unsigned)-(((volume) << 8) / 100))
#define VCHIQ_SOUND_VOLUME_DEFAULT 0

static const char From[] = "sndvchiq";

VCHIQSound::VCHIQSound(CVCHIQDevice *pVCHIQDevice,
                         unsigned nSampleRate,
                         unsigned nChunkSize,
                         TVCHIQSoundDestination Destination)
    : m_nSampleRate(nSampleRate),
      m_nChunkSize(nChunkSize),
      m_Destination(Destination),
      m_State(VCHIQSoundCreated),
      m_VCHIInstance(0),
      m_hService(0),
      m_nWritePos(0),
      m_nCompletePos(0)
{
}

VCHIQSound::~VCHIQSound() {}

boolean VCHIQSound::Start()
{
    if (m_State > VCHIQSoundIdle)
        return FALSE;

    VC_AUDIO_MSG_T Msg;
    int nResult;

    if (m_State == VCHIQSoundCreated)
    {
        nResult = vchi_initialise(&m_VCHIInstance);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "VCHI init failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        nResult = vchi_connect(0, 0, m_VCHIInstance);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "VCHI connect failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        SERVICE_CREATION_T Params = {
            VCHI_VERSION_EX(VC_AUDIOSERV_VER, VC_AUDIOSERV_MIN_VER),
            VC_AUDIO_SERVER_NAME,
            0, 0, 0,
            CallbackStub, this,
            1, 1, 0
        };

        nResult = vchi_service_open(m_VCHIInstance, &Params, &m_hService);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "AUDS open failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        vchi_service_release(m_hService);

        // Configure: stereo, 16-bit
        Msg.type = VC_AUDIO_MSG_TYPE_CONFIG;
        Msg.u.config.channels = 2;
        Msg.u.config.samplerate = m_nSampleRate;
        Msg.u.config.bps = 16;

        nResult = CallMessage(&Msg);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "Config failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        // Set destination (HDMI/headphones)
        Msg.type = VC_AUDIO_MSG_TYPE_CONTROL;
        Msg.u.control.dest = m_Destination;
        Msg.u.control.volume = VOLUME_TO_CHIP(VCHIQ_SOUND_VOLUME_DEFAULT);

        nResult = CallMessage(&Msg);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "Control failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        Msg.type = VC_AUDIO_MSG_TYPE_OPEN;
        nResult = QueueMessage(&Msg);
        if (nResult != 0) {
            CLogger::Get()->Write(From, LogError, "Open failed (%d)", nResult);
            m_State = VCHIQSoundError;
            return FALSE;
        }

        m_State = VCHIQSoundIdle;
    }

    // Start playback
    Msg.type = VC_AUDIO_MSG_TYPE_START;
    nResult = QueueMessage(&Msg);
    if (nResult != 0) {
        CLogger::Get()->Write(From, LogError, "Start failed (%d)", nResult);
        m_State = VCHIQSoundError;
        return FALSE;
    }

    m_State = VCHIQSoundRunning;

    vchi_service_use(m_hService);

    short usPeerVersion = 0;
    nResult = vchi_get_peer_version(m_hService, &usPeerVersion);
    if (nResult != 0 || usPeerVersion < 2) {
        vchi_service_release(m_hService);
        CLogger::Get()->Write(From, LogError, "Peer version error (%d, v%u)",
                              nResult, (unsigned)usPeerVersion);
        m_State = VCHIQSoundError;
        return FALSE;
    }

    m_nWritePos = 0;
    m_nCompletePos = 0;

    // Send two silent chunks to prime the pipeline
    s16 silence[1024];
    memset(silence, 0, sizeof silence);
    WriteChunk(silence, m_nChunkSize > 1024 ? 1024 : m_nChunkSize);
    WriteChunk(silence, m_nChunkSize > 1024 ? 1024 : m_nChunkSize);

    vchi_service_release(m_hService);

    CLogger::Get()->Write(From, LogNotice, "VCHIQ audio started (%u Hz, dest=%d)",
                          m_nSampleRate, m_Destination);
    return TRUE;
}

void VCHIQSound::Cancel()
{
    if (m_State != VCHIQSoundRunning)
        return;

    m_State = VCHIQSoundCancelled;
    while (m_State == VCHIQSoundCancelled && m_nWritePos - m_nCompletePos > 0)
        CScheduler::Get()->Yield();

    if (m_State == VCHIQSoundCancelled)
        m_State = VCHIQSoundTerminating;

    vchi_service_use(m_hService);
    VC_AUDIO_MSG_T Msg;
    Msg.type = VC_AUDIO_MSG_TYPE_STOP;
    Msg.u.stop.draining = 0;
    QueueMessage(&Msg);
    vchi_service_release(m_hService);

    m_State = VCHIQSoundIdle;
}

int VCHIQSound::WriteSamples(const s16 *pBuffer, unsigned nSamples)
{
    if (m_State != VCHIQSoundRunning || !pBuffer || nSamples == 0)
        return 0;

    unsigned nPos = 0;
    while (nPos < nSamples) {
        unsigned nChunk = nSamples - nPos;
        if (nChunk > m_nChunkSize)
            nChunk = m_nChunkSize;

        vchi_service_use(m_hService);
        int nResult = WriteChunk(pBuffer + nPos, nChunk);
        vchi_service_release(m_hService);

        if (nResult != 0)
            return nPos;

        nPos += nChunk;
    }
    return nPos;
}

int VCHIQSound::WriteChunk(const s16 *pData, unsigned nSamples)
{
    if (m_State != VCHIQSoundRunning)
        return -1;

    unsigned nBytes = nSamples * sizeof(s16);

    VC_AUDIO_MSG_T Msg;
    Msg.type = VC_AUDIO_MSG_TYPE_WRITE;
    Msg.u.write.count = nBytes;
    Msg.u.write.max_packet = 4000;
    Msg.u.write.cookie1 = VC_AUDIO_WRITE_COOKIE1;
    Msg.u.write.cookie2 = VC_AUDIO_WRITE_COOKIE2;
    Msg.u.write.silence = 0;

    int nResult = vchi_msg_queue(m_hService, &Msg, sizeof Msg,
                                  VCHI_FLAGS_BLOCK_UNTIL_QUEUED, 0);
    if (nResult != 0)
        return nResult;

    m_nWritePos += nBytes;

    const u8 *pBuf = (const u8 *)pData;
    while (nBytes > 0) {
        unsigned nToQueue = nBytes <= 4000 ? nBytes : 4000;
        nResult = vchi_msg_queue(m_hService, pBuf, nToQueue,
                                  VCHI_FLAGS_BLOCK_UNTIL_QUEUED, 0);
        if (nResult != 0)
            return nResult;
        pBuf += nToQueue;
        nBytes -= nToQueue;
    }

    return 0;
}

int VCHIQSound::CallMessage(VC_AUDIO_MSG_T *pMessage)
{
    m_Event.Clear();
    int nResult = QueueMessage(pMessage);
    if (nResult == 0)
        m_Event.Wait();
    else
        m_nResult = nResult;
    return m_nResult;
}

int VCHIQSound::QueueMessage(VC_AUDIO_MSG_T *pMessage)
{
    vchi_service_use(m_hService);
    int nResult = vchi_msg_queue(m_hService, pMessage, sizeof *pMessage,
                                  VCHI_FLAGS_BLOCK_UNTIL_QUEUED, 0);
    vchi_service_release(m_hService);
    return nResult;
}

void VCHIQSound::Callback(const VCHI_CALLBACK_REASON_T Reason, void *hMessage)
{
    if (Reason != VCHI_CALLBACK_MSG_AVAILABLE)
        return;

    vchi_service_use(m_hService);

    VC_AUDIO_MSG_T Msg;
    uint32_t nMsgLen;
    int nResult = vchi_msg_dequeue(m_hService, &Msg, sizeof Msg,
                                    &nMsgLen, VCHI_FLAGS_NONE);
    if (nResult != 0) {
        vchi_service_release(m_hService);
        m_State = VCHIQSoundError;
        return;
    }

    switch (Msg.type) {
    case VC_AUDIO_MSG_TYPE_RESULT:
        m_nResult = Msg.u.result.success;
        m_Event.Set();
        break;

    case VC_AUDIO_MSG_TYPE_COMPLETE:
        if (m_State >= VCHIQSoundRunning) {
            m_nCompletePos += Msg.u.complete.count & 0x3FFFFFFF;
            if (m_nWritePos - m_nCompletePos == 0 && m_State == VCHIQSoundCancelled)
                m_State = VCHIQSoundTerminating;
        }
        break;

    default:
        break;
    }

    vchi_service_release(m_hService);
}

void VCHIQSound::CallbackStub(void *pParam,
                                const VCHI_CALLBACK_REASON_T Reason,
                                void *hMessage)
{
    VCHIQSound *pThis = (VCHIQSound *)pParam;
    pThis->Callback(Reason, hMessage);
}
