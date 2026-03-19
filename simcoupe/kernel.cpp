// kernel.cpp — SimCoupe Circle kernel (multicore)
//
// Core 0: Initialize() + Run() — USB, IRQs, scheduler
// Core 1: Run(1) — SimCoupe Z80 + video
// Core 2: Run(2) — Sound::FrameUpdate() synthesis

#include <circle/types.h>
#define __time_t_defined
#define _TIME_T_DECLARED

#include "kernel.h"

#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <string.h>

extern "C" void circle_audio_set_interrupt(void *pInterrupt);
extern "C" void circle_audio_set_device(void *pDevice);
extern "C" void circle_audio_start(void);
extern "C" void circle_audio_activate(void *pDevice);
extern "C" void circle_audio_poll(void);
extern "C" int  fatfs_mount(void);
extern "C" int  circle_fb_init(unsigned w, unsigned h, unsigned depth);

namespace Main { bool Init(int argc, char *argv[]); void Exit(); }
namespace CPU  { void Run(); void Exit(); }
namespace Sound { void FrameUpdate(); }

extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt);

static const char FromKernel[] = "kernel";

    // ---- Sound thread signalling (Core 1 → Core 2) ----
volatile bool g_sound_frame_pending = false;
volatile bool g_sound_frame_done    = true;
volatile unsigned g_sound_signal_count = 0;
volatile unsigned g_core2_exec_count = 0;

// ---- USB keyboard handler ----
static unsigned char s_prev_mod = 0;
static unsigned char s_prev_keys[6] = {0};

static const struct { unsigned char mask; unsigned scancode; } s_mod_map[] = {
    { (1<<0), 224 }, { (1<<1), 225 }, { (1<<2), 226 }, { (1<<3), 227 },
    { (1<<4), 228 }, { (1<<5), 229 }, { (1<<6), 230 }, { (1<<7), 231 },
    { 0, 0 }
};

static void KeyStatusHandlerRaw(unsigned char ucMod,
                                 const unsigned char Keys[6],
                                 void* /*arg*/)
{
    int ms = (ucMod & ((1<<1)|(1<<5))) ? 1 : 0;
    int mc = (ucMod & ((1<<0)|(1<<4))) ? 1 : 0;
    int ma = (ucMod & ((1<<2)|(1<<6))) ? 1 : 0;

    unsigned char changed = s_prev_mod ^ ucMod;
    for (int i = 0; s_mod_map[i].mask; i++)
        if (changed & s_mod_map[i].mask)
            circle_simcoupe_key(s_mod_map[i].scancode,
                                (ucMod & s_mod_map[i].mask) ? 1 : 0, 0, 0, 0);
    s_prev_mod = ucMod;

    for (int p = 0; p < 6; p++) {
        unsigned char k = s_prev_keys[p];
        if (!k) continue;
        bool found = false;
        for (int c = 0; c < 6; c++) if (Keys[c] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232) circle_simcoupe_key(k, 0, ms, mc, ma);
    }
    for (int c = 0; c < 6; c++) {
        unsigned char k = Keys[c];
        if (!k) continue;
        bool found = false;
        for (int p = 0; p < 6; p++) if (s_prev_keys[p] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232) circle_simcoupe_key(k, 1, ms, mc, ma);
    }
    memcpy(s_prev_keys, Keys, 6);
}

// ---- CKernel ----

CKernel::CKernel()
:   CMultiCoreSupport(&m_Memory),
    m_Memory(),
    m_Serial(),
    m_Timer(&m_Interrupt),
    m_Logger(m_Options.GetLogLevel(), &m_Timer),
    m_Scheduler(),
    m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
    m_EMMC(&m_Interrupt, &m_Timer),
    m_PWMSound(&m_Interrupt, 44100, 2048),
    m_bLaunch(false)
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

    if (bOK && fatfs_mount() != 0)
        m_Logger.Write(FromKernel, LogWarning, "FatFs mount failed");

    circle_audio_set_interrupt(&m_Interrupt);
    circle_audio_set_device(&m_PWMSound);

    if (bOK && circle_fb_init(800, 600, 8) != 0)
        m_Logger.Write(FromKernel, LogWarning, "Framebuffer init failed");

    // Start secondary cores AFTER VCHIQ init
    if (bOK) bOK = CMultiCoreSupport::Initialize();

    return bOK;
}

// Core 0: USB host + scheduler loop
TShutdownMode CKernel::Run()
{
    m_USBHCI.UpdatePlugAndPlay();

    CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
        CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
    if (pKeyboard)
        pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, nullptr);

    // Start PWM audio — device constructed in kernel ctor, queue setup here
    circle_audio_start();

    // Signal core 1 + core 2 to start
    asm volatile("dmb" ::: "memory");
    m_bLaunch = true;
    asm volatile("dmb" ::: "memory");

    while (true)
    {
        m_Scheduler.Yield();
        m_USBHCI.UpdatePlugAndPlay();
        circle_audio_poll();  // drain ring buffer → Write() on Core 0
    }

    return ShutdownHalt;
}

// Secondary cores
void CKernel::Run(unsigned nCore)
{
    // Wait for core 0
    while (!m_bLaunch)
        asm volatile("dmb" ::: "memory");

    if (nCore == 1)
    {
        // Core 1: Z80 emulation
        static const char *argv0 = "simcoupe";
        static char *argv[] = { const_cast<char*>(argv0), nullptr };

        if (Main::Init(1, argv))
            CPU::Run();

        Main::Exit();
        while (true) asm volatile("wfe");
    }
    else if (nCore == 2)
    {
        // Core 2: Sound synthesis loop
        // Activate audio from this core — pass device pointer directly
        // to avoid cross-core L1 cache visibility issue with s_pSound
        circle_audio_activate(&m_PWMSound);

        while (true)
        {
            asm volatile("dmb" ::: "memory");
            if (g_sound_frame_pending)
            {
                g_sound_frame_pending = false;
                asm volatile("dmb" ::: "memory");

                g_core2_exec_count++;
                Sound::FrameUpdate();

                asm volatile("dmb" ::: "memory");
                g_sound_frame_done = true;
                asm volatile("dmb" ::: "memory");
            }
        }
    }
    else
    {
        // Core 3: unused
        while (true) asm volatile("wfe");
    }
}
