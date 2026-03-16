//
// kernel.cpp
//
// circle-coupe SDL3 integration test kernel.
// Validates video (framebuffer), audio (PWM sine), input (USB HID).
//
#include "kernel.h"

// SDL3 headers (bare-metal build)
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

// Circle utilities
#include <circle/string.h>
#include <circle/util.h>
#include <math.h>   // sinf - provided by newlib via arm-none-eabi

// circle_audio_set_interrupt() must be called before SDL_Init()
extern "C" void circle_audio_set_interrupt (void *pInterrupt);

static const char FromKernel[] = "kernel";

// ---- Sine tone generator -----------------------------------------------

#define TONE_FREQ_HZ    440
#define SAMPLE_RATE     44100
#define CHUNK_FRAMES    1024

static float    s_fPhase     = 0.0f;
static float    s_fPhaseStep = (2.0f * 3.14159265f * TONE_FREQ_HZ) / SAMPLE_RATE;

// SDL3 audio stream callback - fills Sint16 stereo PCM
static void SDLCALL AudioCallback (void *userdata, SDL_AudioStream *stream,
                                    int additional_amount, int /*total_amount*/)
{
    (void)userdata;
    static Sint16 buf[CHUNK_FRAMES * 2];
    int frames = additional_amount / (2 * sizeof(Sint16));
    if (frames <= 0) return;
    if (frames > CHUNK_FRAMES) frames = CHUNK_FRAMES;

    for (int i = 0; i < frames; i++) {
        Sint16 sample = (Sint16)(sinf(s_fPhase) * 16000.0f);
        buf[i * 2]     = sample; // L
        buf[i * 2 + 1] = sample; // R
        s_fPhase += s_fPhaseStep;
        if (s_fPhase >= 2.0f * 3.14159265f) s_fPhase -= 2.0f * 3.14159265f;
    }
    SDL_PutAudioStreamData(stream, buf, frames * 2 * (int)sizeof(Sint16));
}

// ---- Test pattern -------------------------------------------------------

static void DrawTestPattern (SDL_Renderer *renderer, int frame)
{
    // Animated colour bars + frame counter
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    static const SDL_Color bars[] = {
        {255, 255, 255, 255}, {255, 255,   0, 255}, {  0, 255, 255, 255},
        {  0, 255,   0, 255}, {255,   0, 255, 255}, {255,   0,   0, 255},
        {  0,   0, 255, 255}, {  0,   0,   0, 255}
    };
    int w, h;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    int bw = w / 8;

    for (int b = 0; b < 8; b++) {
        SDL_SetRenderDrawColor(renderer,
            bars[b].r, bars[b].g, bars[b].b, bars[b].a);
        SDL_FRect r = { (float)(b * bw), 0.0f, (float)bw, (float)h };
        SDL_RenderFillRect(renderer, &r);
    }

    // Moving white line to show animation
    int y = (frame * 3) % h;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderLine(renderer, 0.0f, (float)y, (float)w, (float)y);
}

// ---- CKernel -----------------------------------------------------------

CKernel::CKernel (void)
:   m_Screen   (m_Options.GetWidth (), m_Options.GetHeight ()),
    m_Serial   (),
    m_Timer    (&m_Interrupt),
    m_Logger   (m_Options.GetLogLevel (), &m_Timer),
    m_Scheduler(),
    m_USBHCI   (&m_Interrupt, &m_Timer, TRUE)  // TRUE = plug-and-play
{
    m_ActLED.Blink (5);
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
    boolean bOK = TRUE;

    if (bOK) bOK = m_Screen.Initialize ();
    if (bOK) bOK = m_Serial.Initialize (115200);

    if (bOK) {
        CDevice *pTarget = m_DeviceNameService.GetDevice (
            m_Options.GetLogDevice (), FALSE);
        if (!pTarget) pTarget = &m_Screen;
        bOK = m_Logger.Initialize (pTarget);
    }

    // CExceptionHandler has no Initialize() - it self-initialises on construction
    if (bOK) bOK = m_Interrupt.Initialize ();
    if (bOK) bOK = m_Timer.Initialize ();
    if (bOK) bOK = m_USBHCI.Initialize ();

    // Must be called BEFORE SDL_Init() so the audio backend can start PWM DMA
    circle_audio_set_interrupt (&m_Interrupt);

    return bOK;
}

TShutdownMode CKernel::Run (void)
{
    m_Logger.Write (FromKernel, LogNotice,
        "circle-coupe SDL3 test kernel - " __DATE__ " " __TIME__);

    RunSDL3Test ();

    return ShutdownHalt;
}

void CKernel::RunSDL3Test (void)
{
    // ---- SDL3 Init -------------------------------------------------------
    if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        m_Logger.Write (FromKernel, LogError, "SDL_Init failed: %s", SDL_GetError ());
        return;
    }
    m_Logger.Write (FromKernel, LogNotice, "SDL_Init OK");

    // ---- Window + Renderer -----------------------------------------------
    SDL_Window   *window   = SDL_CreateWindow ("circle-coupe test", 1280, 720, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer (window, NULL) : NULL;

    if (!renderer) {
        m_Logger.Write (FromKernel, LogError,
            "SDL_CreateRenderer failed: %s", SDL_GetError ());
        SDL_Quit ();
        return;
    }
    m_Logger.Write (FromKernel, LogNotice, "Renderer created OK");

    // ---- Audio stream ----------------------------------------------------
    SDL_AudioSpec spec = {};
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq     = SAMPLE_RATE;

    SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream (
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, NULL);

    if (audio_stream) {
        SDL_ResumeAudioStreamDevice (audio_stream);
        m_Logger.Write (FromKernel, LogNotice, "Audio stream open, playing 440 Hz");
    } else {
        m_Logger.Write (FromKernel, LogWarning, "Audio unavailable: %s", SDL_GetError ());
    }

    // ---- Main loop -------------------------------------------------------
    m_Logger.Write (FromKernel, LogNotice,
        "Running - press ESC to halt, any key logged to serial");

    int  frame      = 0;
    bool bRunning   = true;

    while (bRunning) {
        // Update USB plug-and-play tree (must be called at task level)
        m_USBHCI.UpdatePlugAndPlay ();

        // SDL3 event pump (keyboard/mouse via Circle USB)
        SDL_Event evt;
        while (SDL_PollEvent (&evt)) {
            switch (evt.type) {
            case SDL_EVENT_QUIT:
                bRunning = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                m_Logger.Write (FromKernel, LogNotice,
                    "KEY DOWN: scancode %d sym %d",
                    (int)evt.key.scancode, (int)evt.key.key);
                if (evt.key.scancode == SDL_SCANCODE_ESCAPE) {
                    bRunning = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                m_Logger.Write (FromKernel, LogDebug,
                    "MOUSE: dx=%d dy=%d",
                    (int)evt.motion.xrel, (int)evt.motion.yrel);
                break;
            default:
                break;
            }
        }

        // Audio is driven by Circle PWM DMA - no explicit iterate needed.
        // (ProvidesOwnCallbackThread=true + Circle DMA handles buffer refill)

        // Draw test pattern
        DrawTestPattern (renderer, frame++);
        SDL_RenderPresent (renderer);

        // ~50 fps - SAM Coupe native refresh rate
        m_Timer.MsDelay (20);
    }

    // ---- Cleanup ---------------------------------------------------------
    if (audio_stream) SDL_DestroyAudioStream (audio_stream);
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    m_Logger.Write (FromKernel, LogNotice, "SDL3 test done - halting");
}
