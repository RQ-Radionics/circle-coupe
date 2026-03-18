// Part of SimCoupe - A SAM Coupe emulator
//
// Input.cpp: Circle bare-metal keyboard input
//
// USB HID keyboard events arrive via circle_simcoupe_key() called from
// kernel.cpp's KeyStatusHandlerRaw USB callback (core 0).
// We map USB HID scancodes to SimCoupe HK_ host-key codes and pass them
// to Keyboard::SetKey() and Actions::Key().
//
// Input::Update() is called once per frame from UI::CheckEvents().
// It drains a small ring buffer of pending events pushed by the USB IRQ.

#include "SimCoupe.h"
#include "Input.h"
#include "Actions.h"
#include "GUI.h"
#include "Keyboard.h"
#include "Keyin.h"
#include "Mouse.h"
#include "Options.h"
#include "UI.h"

// ---- Ring buffer for USB key events (IRQ-safe: one writer, one reader) ---

struct KeyEvent {
    unsigned scancode;
    int      pressed;
    int      mod_shift, mod_ctrl, mod_alt;
};

static constexpr int KBD_RING = 64;
static volatile KeyEvent s_ring[KBD_RING];
static volatile int s_ring_head = 0;  // written by USB IRQ (core 0)
static volatile int s_ring_tail = 0;  // read by emulator (core 1)

// Called from kernel.cpp USB IRQ handler (core 0)
extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt)
{
    int next = (s_ring_head + 1) % KBD_RING;
    if (next == s_ring_tail) return;  // overflow: drop

    s_ring[s_ring_head].scancode  = hid_scancode;
    s_ring[s_ring_head].pressed   = pressed;
    s_ring[s_ring_head].mod_shift = mod_shift;
    s_ring[s_ring_head].mod_ctrl  = mod_ctrl;
    s_ring[s_ring_head].mod_alt   = mod_alt;
    asm volatile("dmb" ::: "memory");
    s_ring_head = next;
}

// ---- USB HID scancode → SimCoupe HK_ mapping ----------------------------
// USB HID scancodes match SDL scancodes for most keys.

static int HidToHk(unsigned sc)
{
    // Letters a-z: HID 4-29 → 'a'-'z'
    if (sc >= 4 && sc <= 29) return 'a' + (sc - 4);
    // Digits 1-9: HID 30-38 → '1'-'9'
    if (sc >= 30 && sc <= 38) return '1' + (sc - 30);

    switch (sc)
    {
    case 39:  return '0';
    case 40:  return HK_RETURN;
    case 41:  return HK_ESC;
    case 42:  return HK_BACKSPACE;
    case 43:  return HK_TAB;
    case 44:  return HK_SPACE;
    case 45:  return '-';
    case 46:  return '=';
    case 47:  return '[';
    case 48:  return ']';
    case 49:  return '\\';
    case 51:  return ';';
    case 52:  return '\'';
    case 53:  return '`';
    case 54:  return ',';
    case 55:  return '.';
    case 56:  return '/';
    case 57:  return HK_CAPSLOCK;
    case 58:  return HK_F1;
    case 59:  return HK_F2;
    case 60:  return HK_F3;
    case 61:  return HK_F4;
    case 62:  return HK_F5;
    case 63:  return HK_F6;
    case 64:  return HK_F7;
    case 65:  return HK_F8;
    case 66:  return HK_F9;
    case 67:  return HK_F10;
    case 68:  return HK_F11;
    case 69:  return HK_F12;
    case 73:  return HK_INSERT;
    case 74:  return HK_HOME;
    case 75:  return HK_PGUP;
    case 76:  return HK_DELETE;
    case 77:  return HK_END;
    case 78:  return HK_PGDN;
    case 79:  return HK_RIGHT;
    case 80:  return HK_LEFT;
    case 81:  return HK_DOWN;
    case 82:  return HK_UP;
    case 83:  return HK_NUMLOCK;
    case 84:  return HK_KPDIVIDE;
    case 85:  return HK_KPMULT;
    case 86:  return HK_KPMINUS;
    case 87:  return HK_KPPLUS;
    case 88:  return HK_KPENTER;
    case 89:  return HK_KP1;
    case 90:  return HK_KP2;
    case 91:  return HK_KP3;
    case 92:  return HK_KP4;
    case 93:  return HK_KP5;
    case 94:  return HK_KP6;
    case 95:  return HK_KP7;
    case 96:  return HK_KP8;
    case 97:  return HK_KP9;
    case 98:  return HK_KP0;
    case 99:  return HK_KPDECIMAL;
    case 224: return HK_LCTRL;
    case 225: return HK_LSHIFT;
    case 226: return HK_LALT;
    case 228: return HK_RCTRL;
    case 229: return HK_RSHIFT;
    case 230: return HK_RALT;
    default:  return HK_NONE;
    }
}

// ---- Input API ----------------------------------------------------------

bool Input::Init()
{
    s_ring_head = s_ring_tail = 0;
    Keyboard::Init();
    return true;
}

void Input::Exit()
{
    Keyboard::Purge();
}

void Input::Update()
{
    // Drain ring buffer
    asm volatile("dmb" ::: "memory");
    while (s_ring_tail != s_ring_head)
    {
        KeyEvent ev;
        ev.scancode  = s_ring[s_ring_tail].scancode;
        ev.pressed   = s_ring[s_ring_tail].pressed;
        ev.mod_shift = s_ring[s_ring_tail].mod_shift;
        ev.mod_ctrl  = s_ring[s_ring_tail].mod_ctrl;
        ev.mod_alt   = s_ring[s_ring_tail].mod_alt;
        asm volatile("dmb" ::: "memory");
        s_ring_tail = (s_ring_tail + 1) % KBD_RING;

        int hk = HidToHk(ev.scancode);
        if (hk == HK_NONE) continue;

        bool pressed = ev.pressed != 0;
        int mods = (ev.mod_shift ? HM_LSHIFT : 0) |
                   (ev.mod_ctrl  ? HM_LCTRL  : 0) |
                   (ev.mod_alt   ? HM_LALT   : 0);

        // GUI gets key events directly
        if (GUI::IsActive())
        {
            if (pressed)
                GUI::SendMessage(GM_CHAR, hk, mods);
            continue;
        }

        Keyboard::SetKey(hk, pressed, mods, 0);

        // F-key actions
        if (hk >= HK_F1 && hk <= HK_F12)
        {
            Actions::Key(hk - HK_F1 + 1, pressed,
                         (mods & HM_LCTRL)  != 0,
                         (mods & HM_LALT)   != 0,
                         (mods & HM_LSHIFT) != 0);
        }
    }

    Keyboard::Update();

    // CapsLock / NumLock are toggle keys — release every frame
    Keyboard::SetKey(HK_CAPSLOCK, false);
    Keyboard::SetKey(HK_NUMLOCK,  false);
}

void Input::Purge()
{
    s_ring_head = s_ring_tail = 0;
    Keyboard::Purge();
}

int Input::MapChar(int nChar_, int* /*pnMods_*/)
{
    if (nChar_ > 0 && nChar_ < HK_MAX) return nChar_;
    return 0;
}

int Input::MapKey(int nKey_)
{
    return (nKey_ && nKey_ < HK_MIN) ? nKey_ : HK_NONE;
}

// Mouse stubs — no mouse support yet
bool Input::FilterEvent(void* /*pEvent_*/) { return false; }
bool Input::IsMouseAcquired() { return false; }
void Input::AcquireMouse(bool /*fAcquire_*/) { }
