// Part of SimCoupe - A SAM Coupe emulator
//
// Input.cpp: Circle bare-metal keyboard + gamepad input
//
// USB HID keyboard events arrive via circle_simcoupe_key() called from
// kernel.cpp's KeyStatusHandlerRaw USB callback (core 0).
// USB gamepad events arrive via circle_gamepad_event() called from
// kernel.cpp's GamePadStatusHandler USB callback (core 0).
// We map USB HID scancodes to SimCoupe HK_ host-key codes and pass them
// to Keyboard::SetKey() and Actions::Key().
// Gamepad axes/buttons are mapped to Joystick::SetPosition/SetButtons.
//
// Input::Update() is called once per frame from UI::CheckEvents().
// It drains a small ring buffer of pending events pushed by the USB IRQ.

#include "SimCoupe.h"
#include "Input.h"
#include "Actions.h"
#include "Frame.h"
#include "GUI.h"
#include "GUIDlg.h"
#include "Joystick.h"
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

// ---- Ring buffer for mouse events (IRQ-safe: one writer, one reader) -----

struct MouseEvent {
    int dx, dy;
    unsigned buttons;
};

static constexpr int MOUSE_RING = 64;
static volatile MouseEvent s_mouse_ring[MOUSE_RING];
static volatile int s_mouse_head = 0;
static volatile int s_mouse_tail = 0;
static volatile bool s_mouse_acquired = false;

// Accumulated mouse delta (consumed by Video::MouseRelative via GetMouseDelta)
static volatile int s_mouse_accum_dx = 0;
static volatile int s_mouse_accum_dy = 0;

// GUI cursor position (absolute, in SAM screen coordinates)
static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_cursor_inited = false;

// ---- Ring buffer for gamepad events (IRQ-safe: one writer, one reader) ---

struct GamepadEvent {
    int joystick;
    int buttons;
    int hat_x, hat_y;
};

static constexpr int GPAD_RING = 16;
static volatile GamepadEvent s_gpad_ring[GPAD_RING];
static volatile int s_gpad_head = 0;
static volatile int s_gpad_tail = 0;

// ---- Gamepad axis deadzone (0-32767 range, 15% = ~5000) ----
static constexpr int GAMEPAD_DEADZONE = 5000;

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

// Called from kernel.cpp mouse status handler (USB IRQ context, core 0)
extern "C" void circle_mouse_event(unsigned buttons, int dx, int dy)
{
    int next = (s_mouse_head + 1) % MOUSE_RING;
    if (next == s_mouse_tail) return;  // overflow: drop

    s_mouse_ring[s_mouse_head].dx = dx;
    s_mouse_ring[s_mouse_head].dy = dy;
    s_mouse_ring[s_mouse_head].buttons = buttons;
    asm volatile("dmb" ::: "memory");
    s_mouse_head = next;
}

// Called from kernel.cpp GamePadStatusHandler (USB IRQ context, core 0)
extern "C" void circle_gamepad_event(int joystick, int buttons, int hat_x, int hat_y)
{
    int next = (s_gpad_head + 1) % GPAD_RING;
    if (next == s_gpad_tail) return;  // overflow: drop

    s_gpad_ring[s_gpad_head].joystick = joystick;
    s_gpad_ring[s_gpad_head].buttons  = buttons;
    s_gpad_ring[s_gpad_head].hat_x    = hat_x;
    s_gpad_ring[s_gpad_head].hat_y    = hat_y;
    asm volatile("dmb" ::: "memory");
    s_gpad_head = next;
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
    s_gpad_head = s_gpad_tail = 0;
    Keyboard::Init();
    Joystick::Init();
    return true;
}

void Input::Exit()
{
    Keyboard::Purge();
}

void Input::Update()
{
    // Drain keyboard ring buffer
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

    // Drain gamepad ring buffer
    asm volatile("dmb" ::: "memory");
    while (s_gpad_tail != s_gpad_head)
    {
        GamepadEvent ev;
        ev.joystick = s_gpad_ring[s_gpad_tail].joystick;
        ev.buttons  = s_gpad_ring[s_gpad_tail].buttons;
        ev.hat_x    = s_gpad_ring[s_gpad_tail].hat_x;
        ev.hat_y    = s_gpad_ring[s_gpad_tail].hat_y;
        asm volatile("dmb" ::: "memory");
        s_gpad_tail = (s_gpad_tail + 1) % GPAD_RING;

        if (ev.joystick < 0 || ev.joystick >= MAX_JOYSTICKS)
            continue;

        // Map hat/d-pad to joystick position
        int pos = HJ_CENTRE;
        if (ev.hat_x < 0)  pos |= HJ_LEFT;
        if (ev.hat_x > 0)  pos |= HJ_RIGHT;
        if (ev.hat_y < 0)  pos |= HJ_UP;
        if (ev.hat_y > 0)  pos |= HJ_DOWN;

        Joystick::SetPosition(ev.joystick, pos);

        // Map buttons: bit0=A(fire), bit1=B, bit2=X, bit3=Y
        // Use button A as primary fire, map other buttons to extra fire buttons
        uint32_t joy_buttons = 0;
        if (ev.buttons & 0x01) joy_buttons |= 0x01;  // Button A -> Fire 1
        if (ev.buttons & 0x02) joy_buttons |= 0x02;  // Button B -> Fire 2
        if (ev.buttons & 0x04) joy_buttons |= 0x04;  // Button X -> Fire 3
        if (ev.buttons & 0x08) joy_buttons |= 0x08;  // Button Y -> Fire 4

        Joystick::SetButtons(ev.joystick, joy_buttons);
    }

    // Drain mouse ring buffer
    int mouse_dx = 0, mouse_dy = 0;
    unsigned mouse_buttons = 0;
    bool mouse_events = false;

    asm volatile("dmb" ::: "memory");
    while (s_mouse_tail != s_mouse_head)
    {
        MouseEvent ev;
        ev.dx = s_mouse_ring[s_mouse_tail].dx;
        ev.dy = s_mouse_ring[s_mouse_tail].dy;
        ev.buttons = s_mouse_ring[s_mouse_tail].buttons;
        asm volatile("dmb" ::: "memory");
        s_mouse_tail = (s_mouse_tail + 1) % MOUSE_RING;

        mouse_dx += ev.dx;
        mouse_dy += ev.dy;
        mouse_buttons = ev.buttons;  // latest button state
        mouse_events = true;
    }

    if (mouse_events)
    {
        // Accumulate for Video::MouseRelative()
        s_mouse_accum_dx += mouse_dx;
        s_mouse_accum_dy += mouse_dy;

        // Auto-acquire mouse on first movement
        if (!s_mouse_acquired && (mouse_dx || mouse_dy))
            s_mouse_acquired = true;

        if (GUI::IsActive())
        {
            // GUI mode: track absolute cursor position, send GUI messages
            if (!s_cursor_inited) {
                s_cursor_x = Frame::Width() / 2;
                s_cursor_y = Frame::Height() / 2;
                s_cursor_inited = true;
            }

            s_cursor_x += mouse_dx / 2;
            s_cursor_y += mouse_dy / 2;

            // Clamp to screen
            if (s_cursor_x < 0) s_cursor_x = 0;
            if (s_cursor_y < 0) s_cursor_y = 0;
            if (s_cursor_x >= Frame::Width()) s_cursor_x = Frame::Width() - 1;
            if (s_cursor_y >= Frame::Height()) s_cursor_y = Frame::Height() - 1;

            if (mouse_dx || mouse_dy)
                GUI::SendMessage(GM_MOUSEMOVE, s_cursor_x, s_cursor_y);

            // Button events with current cursor position
            static unsigned s_prev_buttons = 0;
            if ((mouse_buttons & 0x01) && !(s_prev_buttons & 0x01))
                GUI::SendMessage(GM_BUTTONDOWN, s_cursor_x, s_cursor_y);
            if (!(mouse_buttons & 0x01) && (s_prev_buttons & 0x01))
                GUI::SendMessage(GM_BUTTONUP, s_cursor_x, s_cursor_y);
            s_prev_buttons = mouse_buttons;
        }
        else
        {
            // Emulation mode: feed SAM mouse (scaled down for DPI)
            if (pMouse)
            {
                if (mouse_dx || mouse_dy)
                    pMouse->Move(mouse_dx / 4, -mouse_dy / 4);

                pMouse->SetButton(1, (mouse_buttons & 0x01) != 0);
                pMouse->SetButton(2, (mouse_buttons & 0x02) != 0);
                pMouse->SetButton(3, (mouse_buttons & 0x04) != 0);
            }
            s_cursor_inited = false;  // reset for next GUI entry
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
    s_gpad_head = s_gpad_tail = 0;
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

bool Input::FilterEvent(void* /*pEvent_*/) { return false; }

bool Input::IsMouseAcquired()
{
    return s_mouse_acquired;
}

void Input::AcquireMouse(bool fAcquire_)
{
    s_mouse_acquired = fAcquire_;
}

// Called by CircleVideo::MouseRelative() — returns and resets accumulated delta
extern "C" void circle_mouse_get_delta(int *dx, int *dy)
{
    *dx = s_mouse_accum_dx;
    *dy = s_mouse_accum_dy;
    s_mouse_accum_dx = 0;
    s_mouse_accum_dy = 0;
}
