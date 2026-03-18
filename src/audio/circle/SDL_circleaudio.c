/*
 * src/audio/circle/SDL_circleaudio.c
 *
 * SDL3 audio backend for Circle bare-metal (RPi 3B AArch32).
 *
 * Uses Circle CPWMSoundBaseDevice (3.5mm jack, S16LE stereo, 44100 Hz).
 *
 * Model: ProvidesOwnCallbackThread = true
 *   SDL3 does NOT create an audio thread.
 *   Instead, SDL_PlaybackAudioThreadIterate() must be called periodically
 *   from the main kernel loop (see kernel.cpp, task circle-coupe-27k).
 *
 * Flow:
 *   1. OpenDevice  → circle_audio_open()  (starts PWM DMA)
 *   2. GetDeviceBuf→ circle_audio_get_buf() (SDL renders into this)
 *   3. WaitDevice  → circle_audio_wait()  (waits for queue space)
 *   4. PlayDevice  → circle_audio_play()  (enqueues into Circle queue)
 *   5. CloseDevice → circle_audio_close()
 */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_CIRCLE

#include "audio/SDL_sysaudio.h"

/* Implemented in SDL_circleaudio_impl.cpp */
extern void     circle_audio_set_interrupt(void *pInterrupt);
extern int      circle_audio_open(unsigned nSampleRate, unsigned nChannels,
                                   unsigned nSampleFrames);
extern void     circle_audio_close(void);
extern unsigned char *circle_audio_get_buf(unsigned *pSizeOut);
extern void     circle_audio_wait(unsigned nFrames);
extern int      circle_audio_play(const unsigned char *pBuffer, unsigned nBytes);
extern int      circle_audio_is_active(void);
extern unsigned circle_audio_sample_frames(void);

/* ---- Private device data ---- */
struct SDL_PrivateAudioData {
    Uint8   *mixbuf;
    int      mixbuf_size;
};

/* Global device pointer so circle_audio_iterate() can call SDL internals */
static SDL_AudioDevice *s_pDevice = NULL;

/* Called from UI::CheckEvents() each CPU loop iteration to drive audio */
void circle_audio_iterate(void)
{
    if (s_pDevice)
        SDL_PlaybackAudioThreadIterate(s_pDevice);
}

/* ---- Driver callbacks ---- */

static bool CIRCLEAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    s_pDevice = device;
    unsigned nSampleRate  = (unsigned)device->spec.freq;
    unsigned nChannels    = (unsigned)device->spec.channels;
    unsigned nFrames      = circle_audio_sample_frames();

    /* Force S16LE stereo - Circle PWM only supports this */
    device->spec.format   = SDL_AUDIO_S16LE;
    device->spec.channels = 2;
    device->spec.freq     = (int)nSampleRate;
    device->sample_frames = (int)nFrames;
    device->buffer_size   = (int)(nFrames * 2 * sizeof(Sint16));

    if (circle_audio_open(nSampleRate, nChannels, nFrames) != 0) {
        return SDL_SetError("Circle: CPWMSoundBaseDevice failed to start");
    }

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        circle_audio_close();
        return false;
    }

    /* Get the mix buffer allocated by the C++ glue */
    unsigned sz = 0;
    device->hidden->mixbuf      = circle_audio_get_buf(&sz);
    device->hidden->mixbuf_size = (int)sz;

    return true;
}

static void CIRCLEAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    s_pDevice = NULL;
    circle_audio_close();
    if (device->hidden) {
        /* mixbuf is owned by the C++ glue, don't free here */
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static Uint8 *CIRCLEAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    *buffer_size = device->hidden->mixbuf_size;
    return device->hidden->mixbuf;
}

static bool CIRCLEAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    circle_audio_wait((unsigned)device->sample_frames);
    return true;
}

static bool CIRCLEAUDIO_PlayDevice(SDL_AudioDevice *device,
                                    const Uint8 *buffer, int buflen)
{
    return circle_audio_play(buffer, (unsigned)buflen) == 0;
}

/* ---- Driver init ---- */

static bool CIRCLEAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice                   = CIRCLEAUDIO_OpenDevice;
    impl->CloseDevice                  = CIRCLEAUDIO_CloseDevice;
    impl->GetDeviceBuf                 = CIRCLEAUDIO_GetDeviceBuf;
    impl->WaitDevice                   = CIRCLEAUDIO_WaitDevice;
    impl->PlayDevice                   = CIRCLEAUDIO_PlayDevice;

    impl->OnlyHasDefaultPlaybackDevice  = true;
    impl->HasRecordingSupport           = false;
    /* ProvidesOwnCallbackThread=true: SDL does not create an audio thread.
     * SDL_PlaybackAudioThreadIterate() is called from UI::CheckEvents()
     * on every iteration of the CPU loop, which drives WaitDevice+PlayDevice
     * without needing Circle pthreads (which hang on bare-metal). */
    impl->ProvidesOwnCallbackThread     = true;

    return true;
}

AudioBootStrap CIRCLEAUDIO_bootstrap = {
    "circle", "SDL Circle bare-metal audio (PWM)", CIRCLEAUDIO_Init, false, false
};

#endif /* SDL_AUDIO_DRIVER_CIRCLE */
