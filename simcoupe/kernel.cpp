//
// kernel.cpp - SimCoupe Circle kernel for Raspberry Pi 3B
//
// Initializes Circle subsystems, mounts SD card via FatFs,
// then calls SimCoupe's main() to run the emulator.
//
#include "kernel.h"

// SDL3 + Circle audio init
#include <SDL3/SDL.h>
extern "C" void circle_audio_set_interrupt(void *pInterrupt);
extern "C" int  fatfs_mount(void);

// SimCoupe entry point (renamed from main() via #define main SimCoupeMain in OSD.h)
extern "C" int SimCoupeMain(int argc, char *argv[]);

static const char FromKernel[] = "simcoupe-kernel";

// ---- CKernel ----------------------------------------------------------------

CKernel::CKernel()
:   m_Screen (m_Options.GetWidth(), m_Options.GetHeight()),
    m_Serial (),
    m_Timer  (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel(), &m_Timer),
    m_Scheduler(),
    m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
    m_EMMC   (&m_Interrupt, &m_Timer)
{
    m_ActLED.Blink(5);
}

CKernel::~CKernel() {}

boolean CKernel::Initialize()
{
    boolean bOK = TRUE;

    if (bOK) bOK = m_Screen.Initialize();
    if (bOK) bOK = m_Serial.Initialize(115200);

    if (bOK) {
        CDevice *pTarget = m_DeviceNameService.GetDevice(
            m_Options.GetLogDevice(), FALSE);
        if (!pTarget) pTarget = &m_Screen;
        bOK = m_Logger.Initialize(pTarget);
    }

    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    if (bOK) bOK = m_USBHCI.Initialize();

    // SD card (EMMC)
    if (bOK) bOK = m_EMMC.Initialize();

    if (bOK) {
        // Mount FatFs on SD card partition 1
        if (fatfs_mount() != 0) {
            m_Logger.Write(FromKernel, LogWarning,
                "FatFs mount failed - filesystem unavailable");
            // Non-fatal: SimCoupe may still run with embedded ROM
        }
    }

    // Must be called BEFORE SDL_Init() for PWM audio DMA
    circle_audio_set_interrupt(&m_Interrupt);

    return bOK;
}

TShutdownMode CKernel::Run()
{
    m_Logger.Write(FromKernel, LogNotice,
        "SimCoupe Circle - " __DATE__ " " __TIME__);

    // Build argv for SimCoupe:
    // We pass the SD card base path so SimCoupe finds its ROM and config.
    static const char *argv[] = {
        "simcoupe",
        "--rom", "/simcoupe/samcoupe.rom",
        nullptr
    };
    int argc = 3;

    // Drive USB plug-and-play once before starting (enumerate devices)
    m_USBHCI.UpdatePlugAndPlay();

    // Run SimCoupe - this is the main emulator loop
    // Returns when user quits (ESC or window close)
    SimCoupeMain(argc, (char **)argv);

    m_Logger.Write(FromKernel, LogNotice, "SimCoupe exited - halting");
    return ShutdownHalt;
}
