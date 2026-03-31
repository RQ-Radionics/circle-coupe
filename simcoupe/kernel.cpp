// kernel.cpp — SimCoupe Circle kernel (multicore + USB gamepad)
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
#include <circle/screen.h>
#include <circle/startup.h>
#include "Circle/VCHIQSound.h"
#include <circle/util.h>
#include <string.h>

// Audio configuration: sample rate and chunk size
// Circle PWM DMA requires minimum chunk size (typically 128+ samples)
// Using 512 as safe default, sample rate at 22050 for better quality than 8k
constexpr unsigned SAMPLE_RATE = 44100;
constexpr unsigned CHUNK_SIZE  = 512;  // Safe DMA size (~23ms @ 22050Hz)

extern "C" void circle_audio_set_interrupt(void *pInterrupt);
extern "C" void circle_audio_set_device(void *pDevice);
extern "C" void circle_audio_set_vchiq_device(void *pDevice);
extern "C" void circle_audio_set_hdmi_polling(void *pDevice);
extern "C" void circle_audio_start(void);
extern "C" void circle_audio_activate(void *pDevice);
extern "C" void circle_audio_poll(void);
extern "C" int  fatfs_mount(void);
extern "C" int  circle_fb_init(unsigned w, unsigned h, unsigned depth);
extern "C" void *circle_fb_get_screen_device(void);
extern "C" void *circle_fb_get_buffer(void);
extern "C" unsigned circle_fb_get_pitch(void);
extern "C" unsigned circle_fb_get_height(void);

namespace Main { bool Init(int argc, char *argv[]); void Exit(); }
namespace CPU  { void Run(); void Exit(); }
namespace Sound { void FrameUpdate(); }

extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt,
                                     unsigned char raw_mod);
extern "C" void circle_mouse_event(unsigned buttons, int dx, int dy);

// Gamepad event callback (defined in Circle/Input.cpp)
extern "C" void circle_gamepad_event(int joystick, int buttons, int hat_x, int hat_y);

static const char FromKernel[] = "kernel";

// ---- USB mouse handler ----
static void MouseStatusHandler(unsigned nButtons, int nDisplacementX,
                                int nDisplacementY, int nWheelMove)
{
    circle_mouse_event(nButtons, nDisplacementX, nDisplacementY);
}

    // ---- Sound thread signalling (Core 1 → Core 2) ----
volatile bool g_sound_frame_pending = false;
volatile bool g_sound_frame_done    = true;
volatile unsigned g_sound_signal_count = 0;
volatile unsigned g_core2_exec_count = 0;
volatile bool g_reboot_requested = false;

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

    // CTRL-ALT-DELETE: reboot the Raspberry Pi
    if (mc && ma)
    {
        for (int c = 0; c < 6; c++)
        {
            if (Keys[c] == 0x4C)  // HID scancode for DELETE
            {
                void *fb = circle_fb_get_buffer();
                if (fb)
                    memset(fb, 0, circle_fb_get_pitch() * circle_fb_get_height());
                reboot();
            }
        }
    }

    unsigned char changed = s_prev_mod ^ ucMod;
    for (int i = 0; s_mod_map[i].mask; i++)
        if (changed & s_mod_map[i].mask)
            circle_simcoupe_key(s_mod_map[i].scancode,
                                (ucMod & s_mod_map[i].mask) ? 1 : 0, 0, 0, 0, 0);
    s_prev_mod = ucMod;

    for (int p = 0; p < 6; p++) {
        unsigned char k = s_prev_keys[p];
        if (!k) continue;
        bool found = false;
        for (int c = 0; c < 6; c++) if (Keys[c] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232) circle_simcoupe_key(k, 0, ms, mc, ma, ucMod);
    }
    for (int c = 0; c < 6; c++) {
        unsigned char k = Keys[c];
        if (!k) continue;
        bool found = false;
        for (int p = 0; p < 6; p++) if (s_prev_keys[p] == k) { found = true; break; }
        if (!found && k >= 4 && k < 232) circle_simcoupe_key(k, 1, ms, mc, ma, ucMod);
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
    m_VCHIQ(CMemorySystem::Get(), &m_Interrupt),
    m_pSound(nullptr),
    m_bLaunch(false)
{
    for (unsigned i = 0; i < MAX_GAMEPADS; i++)
        m_pGamePad[i] = nullptr;
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

    // CScreenDevice inside circle_fb_init anchors HDMI — survives VCHIQ init
    if (bOK && circle_fb_init(800, 600, 8) != 0)
        m_Logger.Write(FromKernel, LogWarning, "Framebuffer init failed");

    // Redirect logger to screen so we can see output on HDMI
    CScreenDevice *pScreen = (CScreenDevice *)circle_fb_get_screen_device();
    if (pScreen)
        m_Logger.Initialize(pScreen);

    circle_audio_set_interrupt(&m_Interrupt);

    // Audio destination: sounddev=hdmi|headphones in cmdline.txt
    // If not specified, auto-detect: Pi 4/400 → HDMI, Pi 2/3 → headphones
    const char *pSoundDevice = m_Options.GetSoundDevice();
    if (strcmp(pSoundDevice, "sndhdmi") == 0 || strcmp(pSoundDevice, "hdmi") == 0)
        m_bHDMI = true;
    else if (strcmp(pSoundDevice, "headphones") == 0 || strcmp(pSoundDevice, "sndpwm") == 0)
        m_bHDMI = false;
    else
        m_bHDMI = (RASPPI >= 4);  // auto: Pi 4/400 → HDMI, Pi 2/3 → headphones

    m_Logger.Write(FromKernel, LogNotice, "Sound: VCHIQ %s", m_bHDMI ? "HDMI" : "headphones");

    if (bOK && m_VCHIQ.Initialize())
        m_pSound = nullptr;  // VCHIQSound created in Run() where scheduler is active
    else
        m_Logger.Write(FromKernel, LogWarning, "VCHIQ init failed — no audio");

    if (bOK) bOK = CMultiCoreSupport::Initialize();

    return bOK;
}

// Core 0: USB host + scheduler loop
TShutdownMode CKernel::Run()
{
    m_USBHCI.UpdatePlugAndPlay();

    // Signal core 1 + core 2 to start
    asm volatile("dmb" ::: "memory");
    m_bLaunch = true;
    asm volatile("dmb" ::: "memory");

    // VCHIQ audio: create in main loop where scheduler is active
    bool bAudioStarted = false;

    while (true)
    {
        m_Scheduler.Yield();
        circle_audio_poll();  // drain Core 2 audio ring → VCHIQ on Core 0

        // Check if Core 1 requested reboot (Shift+F12)
        if (g_reboot_requested)
        {
            m_Timer.MsDelay(500);  // let screen clear
            reboot();
        }

        if (!bAudioStarted)
        {
            TVCHIQSoundDestination dest = m_bHDMI
                ? VCHIQSoundDestinationHDMI
                : VCHIQSoundDestinationHeadphones;
            auto *pVCHIQ = new VCHIQSound(&m_VCHIQ, SAMPLE_RATE, 1024, dest);
            circle_audio_set_vchiq_device(pVCHIQ);
            circle_audio_start();
            bAudioStarted = true;
        }
        m_USBHCI.UpdatePlugAndPlay();

        // Check for keyboard (hot-plug support)
        CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
            CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
        static CUSBKeyboardDevice *s_pLastKeyboard = nullptr;

        if (pKeyboard != s_pLastKeyboard)
        {
            if (pKeyboard != nullptr)
            {
                pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, nullptr);
                m_Logger.Write(FromKernel, LogNotice, "Keyboard connected");
            }
            s_pLastKeyboard = pKeyboard;
        }

        // Check for mouse (hot-plug support)
        CMouseDevice *pMouse = (CMouseDevice *)
            CDeviceNameService::Get()->GetDevice("mouse1", FALSE);
        static CMouseDevice *s_pLastMouse = nullptr;

        if (pMouse != s_pLastMouse)
        {
            if (pMouse != nullptr)
            {
                pMouse->RegisterStatusHandler(MouseStatusHandler);
                m_Logger.Write(FromKernel, LogNotice, "Mouse connected");
            }
            s_pLastMouse = pMouse;
        }

        // Check for newly connected gamepads
        for (unsigned nDevice = 1; nDevice <= MAX_GAMEPADS; nDevice++)
        {
            if (m_pGamePad[nDevice-1] != nullptr)
                continue;

            m_pGamePad[nDevice-1] = (CUSBGamePadDevice *)
                CDeviceNameService::Get()->GetDevice("upad", nDevice, FALSE);
            if (m_pGamePad[nDevice-1] == nullptr)
                continue;

            const TGamePadState *pState = m_pGamePad[nDevice-1]->GetInitialState();
            if (pState)
            {
                m_Logger.Write(FromKernel, LogNotice,
                    "Gamepad %u: %d buttons, %d axes, %d hats",
                    nDevice, pState->nbuttons, pState->naxes, pState->nhats);
            }

            m_pGamePad[nDevice-1]->RegisterRemovedHandler(GamePadRemovedHandler, this);
            m_pGamePad[nDevice-1]->RegisterStatusHandler(GamePadStatusHandler);
        }
    }

    return ShutdownHalt;
}

// Gamepad state tracking for change detection
static unsigned s_last_buttons[MAX_GAMEPADS] = {0, 0};
static int s_last_hat[MAX_GAMEPADS] = {0, 0};
static int s_last_axis_x[MAX_GAMEPADS] = {0, 0};
static int s_last_axis_y[MAX_GAMEPADS] = {0, 0};

void CKernel::GamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState)
{
    if (nDeviceIndex >= MAX_GAMEPADS) return;

    unsigned buttons = pState->buttons;
    int hat = pState->nhats > 0 ? pState->hats[0] : -1;

    int hat_x = 0, hat_y = 0;
    if (hat >= 1 && hat <= 8) {
        if (hat == 1 || hat == 2 || hat == 8) hat_y = -1;
        if (hat == 4 || hat == 5 || hat == 6) hat_y = 1;
        if (hat == 2 || hat == 3 || hat == 4) hat_x = 1;
        if (hat == 6 || hat == 7 || hat == 8) hat_x = -1;
    }
    else if (pState->naxes >= 2) {
        int centerX = (pState->axes[0].minimum + pState->axes[0].maximum) / 2;
        int centerY = (pState->axes[1].minimum + pState->axes[1].maximum) / 2;
        int rangeX = (pState->axes[0].maximum - pState->axes[0].minimum) / 4;
        int rangeY = (pState->axes[1].maximum - pState->axes[1].minimum) / 4;

        if (pState->axes[0].value < centerX - rangeX) hat_x = -1;
        if (pState->axes[0].value > centerX + rangeX) hat_x = 1;
        if (pState->axes[1].value < centerY - rangeY) hat_y = -1;
        if (pState->axes[1].value > centerY + rangeY) hat_y = 1;
    }

    if (buttons == s_last_buttons[nDeviceIndex] &&
        hat == s_last_hat[nDeviceIndex] &&
        hat_x == s_last_axis_x[nDeviceIndex] &&
        hat_y == s_last_axis_y[nDeviceIndex])
        return;

    s_last_buttons[nDeviceIndex] = buttons;
    s_last_hat[nDeviceIndex] = hat;
    s_last_axis_x[nDeviceIndex] = hat_x;
    s_last_axis_y[nDeviceIndex] = hat_y;

    circle_gamepad_event(nDeviceIndex, (int)buttons, hat_x, hat_y);
}

void CKernel::GamePadRemovedHandler(CDevice *pDevice, void *pContext)
{
    CKernel *pThis = (CKernel *)pContext;
    for (unsigned i = 0; i < MAX_GAMEPADS; i++)
    {
        if (pThis->m_pGamePad[i] == (CUSBGamePadDevice *)pDevice)
        {
            pThis->m_Logger.Write(FromKernel, LogDebug, "Gamepad %u removed", i + 1);
            pThis->m_pGamePad[i] = nullptr;
            break;
        }
    }
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

        // Clear screen to black before exiting video subsystem
        void *fb = circle_fb_get_buffer();
        if (fb)
            memset(fb, 0, circle_fb_get_pitch() * circle_fb_get_height());

        Main::Exit();

        // Request reboot on core 0
        asm volatile("dmb" ::: "memory");
        g_reboot_requested = true;
        asm volatile("dmb" ::: "memory");

        while (true) asm volatile("wfe");
    }
    else if (nCore == 2)
    {
        // Core 2: Sound synthesis loop
        circle_audio_activate(m_pSound);

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
