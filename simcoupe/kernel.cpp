//
// kernel.cpp - SimCoupe Circle kernel for Raspberry Pi 3B
//
// Multi-core:
//   Core 0: Initialize() then Run() -- IO loop (USB, scheduler yield)
//   Core 1: Run(1) -- SimCoupeMain() -- full Z80+video+audio emulation
//
#include "kernel.h"

#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <string.h>

extern "C" void circle_audio_set_interrupt(void *pInterrupt);
extern "C" int  fatfs_mount(void);
extern "C" int  SimCoupeMain(int argc, char *argv[]);

// Direct SimCoupe key injection -- implemented in SDL/Input.cpp
extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt);

static const char FromKernel[] = "simcoupe-kernel";

// ---- USB keyboard handler ----

static unsigned char s_prev_modifiers = 0;
static unsigned char s_prev_keys[6] = {0};

static const struct { unsigned char mask; unsigned scancode; } s_mod_map[] = {
    { (1<<0), 224 }, // SDL_SCANCODE_LCTRL
    { (1<<1), 225 }, // SDL_SCANCODE_LSHIFT
    { (1<<2), 226 }, // SDL_SCANCODE_LALT
    { (1<<3), 227 }, // SDL_SCANCODE_LGUI
    { (1<<4), 228 }, // SDL_SCANCODE_RCTRL
    { (1<<5), 229 }, // SDL_SCANCODE_RSHIFT
    { (1<<6), 230 }, // SDL_SCANCODE_RALT
    { (1<<7), 231 }, // SDL_SCANCODE_RGUI
    { 0,      0   }
};

static void KeyStatusHandlerRaw(unsigned char ucModifiers,
                                 const unsigned char RawKeys[6],
                                 void * /*arg*/)
{
    int mshift = (ucModifiers & ((1<<1)|(1<<5))) ? 1 : 0;
    int mctrl  = (ucModifiers & ((1<<0)|(1<<4))) ? 1 : 0;
    int malt   = (ucModifiers & ((1<<2)|(1<<6))) ? 1 : 0;

    unsigned char mod_changed = s_prev_modifiers ^ ucModifiers;
    for (int i = 0; s_mod_map[i].mask; i++) {
        if (mod_changed & s_mod_map[i].mask)
            circle_simcoupe_key(s_mod_map[i].scancode,
                                (ucModifiers & s_mod_map[i].mask) ? 1 : 0,
                                0, 0, 0);
    }
    s_prev_modifiers = ucModifiers;

    for (int p = 0; p < 6; p++) {
        unsigned char k = s_prev_keys[p];
        if (k == 0) continue;
        bool found = false;
        for (int c = 0; c < 6; c++)
            if (RawKeys[c] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232)
            circle_simcoupe_key(k, 0, mshift, mctrl, malt);
    }

    for (int c = 0; c < 6; c++) {
        unsigned char k = RawKeys[c];
        if (k == 0) continue;
        bool found = false;
        for (int p = 0; p < 6; p++)
            if (s_prev_keys[p] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232)
            circle_simcoupe_key(k, 1, mshift, mctrl, malt);
    }

    memcpy(s_prev_keys, RawKeys, 6);
}

static void MouseStatusHandler(unsigned /*nButtons*/,
                                int /*nDX*/, int /*nDY*/, int /*nWheel*/,
                                void * /*pParam*/)
{
    // TODO: mouse support
}

// ---- CKernel ----------------------------------------------------------------

CKernel::CKernel()
:   CMultiCoreSupport (&m_Memory),
    m_Memory (),
    m_bLaunch (false),
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

    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    if (bOK) bOK = m_USBHCI.Initialize();
    if (bOK) bOK = m_EMMC.Initialize();

    if (bOK) {
        if (fatfs_mount() != 0)
            m_Logger.Write(FromKernel, LogWarning, "FatFs mount failed");
    }

    circle_audio_set_interrupt(&m_Interrupt);

    // Start secondary cores FIRST -- Run(1) will spin on m_bLaunch
    if (bOK) bOK = CMultiCoreSupport::Initialize();

    return bOK;
}

// Core 0: IO loop -- keeps USB + scheduler alive while core 1 runs the emulator
TShutdownMode CKernel::Run()
{
    m_USBHCI.UpdatePlugAndPlay();

    // Register USB devices
    CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
        CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
    if (pKeyboard)
        pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, nullptr);

    CMouseDevice *pMouse = (CMouseDevice *)
        CDeviceNameService::Get()->GetDevice("mouse1", FALSE);
    if (pMouse)
        pMouse->RegisterStatusHandler(MouseStatusHandler, nullptr);

    // Signal core 1 to start the emulator - data barrier ensures visibility
    asm volatile("dmb" ::: "memory");
    m_bLaunch = true;
    asm volatile("dmb" ::: "memory");

    // Core 0 loops forever servicing USB and scheduler
    while (true)
    {
        m_Scheduler.Yield();
        m_USBHCI.UpdatePlugAndPlay();
    }

    return ShutdownHalt;
}

// Core 1: emulator -- waits for core 0 to finish init, then runs SimCoupeMain()
void CKernel::Run(unsigned nCore)
{
    if (nCore != 1) {
        // Cores 2 and 3: not used, park them
        while (true) { asm volatile("wfe"); }
    }

    // Spin until core 0 completes Initialize() and USB setup
    while (!m_bLaunch) {
        asm volatile("dmb" ::: "memory");
    }

    static const char *argv[] = {
        "simcoupe",
        "--disk1", "/simcoupe/autoload.dsk",
        nullptr
    };
    int argc = 3;

    SimCoupeMain(argc, (char **)argv);

    while (true) { asm volatile("wfe"); }
}
