// kernel.cpp — SimCoupe Circle kernel (no SDL, monocore)
//
// Identical structure to the working kernel (75227bc) but calls
// Main::Init() + CPU::Run() instead of SimCoupeMain() (which used SDL).

#include "kernel.h"

#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <string.h>

extern "C" void circle_audio_set_interrupt(void *pInterrupt);
extern "C" int  fatfs_mount(void);
extern "C" int      circle_fb_init(unsigned w, unsigned h, unsigned depth);
extern "C" unsigned circle_fb_get_width(void);
extern "C" unsigned circle_fb_get_height(void);
extern "C" unsigned circle_fb_get_pitch(void);
extern "C" void    *circle_fb_get_buffer(void);
extern "C" void     circle_fb_flip(void);

// circle_simcoupe_key is defined in Circle/Input.cpp
extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt);

// SimCoupe entry points (no SDL)
namespace Main { bool Init(int argc, char *argv[]); void Exit(); }
namespace CPU  { void Run(); void Exit(); }

static const char FromKernel[] = "kernel";

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

// ---- CKernel ----------------------------------------------------------------

CKernel::CKernel()
:   m_Serial(),
    m_Timer(&m_Interrupt),
    m_Logger(m_Options.GetLogLevel(), &m_Timer),
    m_Scheduler(),
    m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
    m_EMMC(&m_Interrupt, &m_Timer)
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

    if (bOK && circle_fb_init(1280, 720, 32) != 0)
        m_Logger.Write(FromKernel, LogWarning, "Framebuffer init failed");

    return bOK;
}

TShutdownMode CKernel::Run()
{
    m_USBHCI.UpdatePlugAndPlay();

    CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
        CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
    if (pKeyboard)
        pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE, nullptr);

    static const char *argv0 = "simcoupe";
    static char *argv[] = { const_cast<char*>(argv0), nullptr };

    if (Main::Init(1, argv))
        CPU::Run();

    Main::Exit();
    return ShutdownHalt;
}
