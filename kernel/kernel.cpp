//
// kernel.cpp
//
// circle-coupe SDL3 integration test kernel.
// Validates video, audio (PWM 440Hz), USB input.
// Designed for use WITHOUT serial - all status shown on screen via SDL_RenderDebugText.
//
#include "kernel.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <circle/string.h>
#include <circle/util.h>
#include <math.h>

extern "C" void circle_audio_set_interrupt (void *pInterrupt);

static const char FromKernel[] = "kernel";

// ---- Constants ---------------------------------------------------------
#define TONE_FREQ_HZ    440
#define SAMPLE_RATE     44100
#define CHUNK_FRAMES    1024
#define SCREEN_W        1280
#define SCREEN_H        720

// ---- Sine tone generator -----------------------------------------------

static float s_fPhase     = 0.0f;
static float s_fPhaseStep = (2.0f * 3.14159265f * TONE_FREQ_HZ) / SAMPLE_RATE;

static void SDLCALL AudioCallback (void *userdata, SDL_AudioStream *stream,
                                    int additional_amount, int /*total_amount*/)
{
    (void)userdata;
    static Sint16 buf[CHUNK_FRAMES * 2];
    int frames = additional_amount / (2 * (int)sizeof(Sint16));
    if (frames <= 0) return;
    if (frames > CHUNK_FRAMES) frames = CHUNK_FRAMES;

    for (int i = 0; i < frames; i++) {
        Sint16 sample = (Sint16)(sinf(s_fPhase) * 16000.0f);
        buf[i * 2]     = sample;
        buf[i * 2 + 1] = sample;
        s_fPhase += s_fPhaseStep;
        if (s_fPhase >= 2.0f * 3.14159265f)
            s_fPhase -= 2.0f * 3.14159265f;
    }
    SDL_PutAudioStreamData(stream, buf, frames * 2 * (int)sizeof(Sint16));
}

// ---- OSD helpers --------------------------------------------------------

// Render a status line using SDL3's built-in 8x8 debug font
static void OSD (SDL_Renderer *r, int row, SDL_Color col, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    // SDL_FONT_MAX_SIZE = 8; each char is 8x8 + 1 pixel gap -> ~9px tall
    SDL_RenderDebugText(r, 8.0f, (float)(8 + row * 12), buf);
}

// ---- Colour bar pattern -------------------------------------------------

static void DrawTestPattern (SDL_Renderer *renderer, int frame,
                              bool bAudio, bool bKeyboard,
                              const char *lastKey, unsigned nFrames)
{
    // Background
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderClear(renderer);

    // Colour bars (top 60%)
    static const SDL_Color bars[8] = {
        {255,255,255,255}, {255,255,  0,255}, {  0,255,255,255}, {  0,255,  0,255},
        {255,  0,255,255}, {255,  0,  0,255}, {  0,  0,255,255}, { 20, 20, 20,255}
    };
    int bh = (SCREEN_H * 6) / 10;
    int bw = SCREEN_W / 8;
    for (int b = 0; b < 8; b++) {
        SDL_SetRenderDrawColor(renderer, bars[b].r, bars[b].g, bars[b].b, 255);
        SDL_FRect r = { (float)(b * bw), 0.0f, (float)bw, (float)bh };
        SDL_RenderFillRect(renderer, &r);
    }

    // Animated white line (proves CPU is running)
    int y = bh/2 + (int)(sinf((float)frame * 0.05f) * (float)(bh/3));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderLine(renderer, 0.0f, (float)y, (float)SCREEN_W, (float)y);

    // ---- OSD status panel (bottom 40%) ----
    float py = (float)bh + 8.0f;
    SDL_SetRenderDrawColor(renderer, 0, 0, 80, 255);
    SDL_FRect panel = { 0, py, (float)SCREEN_W, (float)(SCREEN_H - bh) };
    SDL_RenderFillRect(renderer, &panel);

    SDL_Color WHITE  = {255,255,255,255};
    SDL_Color GREEN  = { 80,255, 80,255};
    SDL_Color RED    = {255, 80, 80,255};
    SDL_Color YELLOW = {255,255,  0,255};

    int row = (int)((py) / 12.0f);   // first OSD row index

    OSD(renderer, row,   WHITE,  "circle-coupe SDL3 test  build: " __DATE__);
    OSD(renderer, row+1, WHITE,  "frame: %u  (~50fps = SAM Coupe)", nFrames);
    OSD(renderer, row+2,
        bAudio ? GREEN : RED,
        "AUDIO: %s  (jack 3.5mm, 440Hz sine - should BEEP)",
        bAudio ? "OK - stream open" : "FAILED - check audio backend log");
    OSD(renderer, row+3,
        bKeyboard ? GREEN : YELLOW,
        "KEYBOARD: %s",
        bKeyboard ? "USB detected - type something" : "waiting for USB keyboard...");
    OSD(renderer, row+4, WHITE,
        "LAST KEY: %s", (lastKey && lastKey[0]) ? lastKey : "(none yet)");
    OSD(renderer, row+5, WHITE,
        "MOUSE:    move mouse - relative motion via USB");
    OSD(renderer, row+6, YELLOW,
        "Press ESC to halt.");

    // Frame counter bar (thin strip at bottom - visual heartbeat)
    int barw = (frame % SCREEN_W);
    SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
    SDL_FRect hb = { 0, (float)(SCREEN_H - 4), (float)barw, 4.0f };
    SDL_RenderFillRect(renderer, &hb);
}

// ---- CKernel -----------------------------------------------------------

CKernel::CKernel (void)
:   m_Screen   (m_Options.GetWidth (), m_Options.GetHeight ()),
    m_Serial   (),
    m_Timer    (&m_Interrupt),
    m_Logger   (m_Options.GetLogLevel (), &m_Timer),
    m_Scheduler(),
    m_USBHCI   (&m_Interrupt, &m_Timer, TRUE)
{
    m_ActLED.Blink (5);
}

CKernel::~CKernel (void) {}

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

    if (bOK) bOK = m_Interrupt.Initialize ();
    if (bOK) bOK = m_Timer.Initialize ();
    if (bOK) bOK = m_USBHCI.Initialize ();

    // MUST be called before SDL_Init() so PWM DMA starts with valid interrupt ptr
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
        // Can't log to screen - SDL failed. Blink the ACT LED SOS pattern.
        for (;;) {
            m_ActLED.Blink(3); m_Timer.MsDelay(500);
            m_ActLED.Blink(3); m_Timer.MsDelay(500);
            m_ActLED.Blink(3); m_Timer.MsDelay(1000);
        }
    }

    // ---- Window + Renderer -----------------------------------------------
    SDL_Window   *window   = SDL_CreateWindow ("circle-coupe", SCREEN_W, SCREEN_H, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer (window, NULL) : NULL;

    if (!renderer) {
        SDL_Quit ();
        // Halt - can't render
        for (;;) { m_ActLED.Blink(6); m_Timer.MsDelay(1000); }
    }

    // ---- Audio stream ----------------------------------------------------
    bool bAudio = false;
    SDL_AudioStream *audio_stream = NULL;

    SDL_AudioSpec spec = {};
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq     = SAMPLE_RATE;

    audio_stream = SDL_OpenAudioDeviceStream (
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, NULL);

    if (audio_stream) {
        SDL_ResumeAudioStreamDevice (audio_stream);
        bAudio = true;
    }

    // ---- Main loop -------------------------------------------------------
    char     lastKey[32]  = "";
    bool     bKeyboard    = false;
    bool     bRunning     = true;
    unsigned nFrames      = 0;

    while (bRunning) {
        // Update USB tree (plug-and-play)
        m_USBHCI.UpdatePlugAndPlay ();

        // SDL3 event pump
        SDL_Event evt;
        while (SDL_PollEvent (&evt)) {
            switch (evt.type) {
            case SDL_EVENT_QUIT:
                bRunning = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                bKeyboard = true;
                if (evt.key.scancode == SDL_SCANCODE_ESCAPE) {
                    bRunning = false;
                    break;
                }
                // Show scancode + key name on screen
                SDL_snprintf(lastKey, sizeof(lastKey),
                    "scancode=%d  sym=%d  '%s'",
                    (int)evt.key.scancode,
                    (int)evt.key.key,
                    SDL_GetKeyName(evt.key.key));
                m_Logger.Write(FromKernel, LogNotice,
                    "KEY DOWN: %s", lastKey);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_Logger.Write(FromKernel, LogDebug,
                    "MOUSE: dx=%d dy=%d buttons=%u",
                    (int)evt.motion.xrel,
                    (int)evt.motion.yrel,
                    evt.motion.state);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                SDL_snprintf(lastKey, sizeof(lastKey),
                    "MOUSE BUTTON %d DOWN", (int)evt.button.button);
                bKeyboard = true;
                break;

            default:
                break;
            }
        }

        // Render
        DrawTestPattern(renderer, (int)nFrames, bAudio, bKeyboard,
                        lastKey, nFrames);
        SDL_RenderPresent (renderer);

        nFrames++;

        // ~50 fps
        m_Timer.MsDelay (20);
    }

    // ---- Cleanup ---------------------------------------------------------
    if (audio_stream) SDL_DestroyAudioStream (audio_stream);
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    m_Logger.Write (FromKernel, LogNotice, "SDL3 test done - halting");
}
