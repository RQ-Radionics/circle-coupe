/*
 * src/events/circle/SDL_circleevents_impl.cpp
 *
 * Circle USB keyboard/mouse -> SDL3 event injection.
 *
 * The Circle USB keyboard callback is called from task context (USB scheduler),
 * NOT from IRQ context. We can therefore call SDL_SendKeyboardKey directly
 * from the callback without needing a ring buffer.
 */

#include <stdint.h>
#include <string.h>

#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>

/* SDL3 injection functions -- set by circle_input_set_callbacks() */
typedef void (*fn_key_t)          (unsigned scancode, int down, unsigned long long ts);
typedef void (*fn_mouse_motion_t) (int dx, int dy, unsigned long long ts);
typedef void (*fn_mouse_button_t) (unsigned button, int down, unsigned long long ts);
typedef void (*fn_mouse_wheel_t)  (int dy, unsigned long long ts);

static fn_key_t           s_fn_key    = nullptr;
static fn_mouse_motion_t  s_fn_motion = nullptr;
static fn_mouse_button_t  s_fn_button = nullptr;
static fn_mouse_wheel_t   s_fn_wheel  = nullptr;

/* ---- Keyboard state tracking ---- */

static uint8_t s_prev_modifiers = 0;
static uint8_t s_prev_keys[6]   = {0};

/* USB HID modifier byte -> SDL scancode mapping */
struct ModMapEntry { uint8_t mask; uint8_t scancode; };
static const ModMapEntry s_mod_map[] = {
    { (1<<0), 224 }, /* SDL_SCANCODE_LCTRL  */
    { (1<<1), 225 }, /* SDL_SCANCODE_LSHIFT */
    { (1<<2), 226 }, /* SDL_SCANCODE_LALT   */
    { (1<<3), 227 }, /* SDL_SCANCODE_LGUI   */
    { (1<<4), 228 }, /* SDL_SCANCODE_RCTRL  */
    { (1<<5), 229 }, /* SDL_SCANCODE_RSHIFT */
    { (1<<6), 230 }, /* SDL_SCANCODE_RALT   */
    { (1<<7), 231 }, /* SDL_SCANCODE_RGUI   */
    { 0,      0   }
};

static void key_status_handler(unsigned char ucModifiers,
                                const unsigned char RawKeys[6],
                                void * /*arg*/)
{
    if (!s_fn_key) return;

    unsigned long long ts = (unsigned long long)CTimer::GetClockTicks64();

    /* --- Modifier changes --- */
    uint8_t mod_changed = s_prev_modifiers ^ ucModifiers;
    for (int i = 0; s_mod_map[i].mask; i++) {
        if (mod_changed & s_mod_map[i].mask) {
            int down = (ucModifiers & s_mod_map[i].mask) ? 1 : 0;
            s_fn_key(s_mod_map[i].scancode, down, ts);
        }
    }
    s_prev_modifiers = ucModifiers;

    /* --- Key releases: in prev but not in current --- */
    for (int p = 0; p < 6; p++) {
        uint8_t k = s_prev_keys[p];
        if (k == 0) continue;
        bool found = false;
        for (int c = 0; c < 6; c++) {
            if (RawKeys[c] == k) { found = true; break; }
        }
        if (!found && k >= 4 && k < 232)
            s_fn_key(k, 0, ts);
    }

    /* --- Key presses: in current but not in prev --- */
    for (int c = 0; c < 6; c++) {
        uint8_t k = RawKeys[c];
        if (k == 0) continue;
        bool found = false;
        for (int p = 0; p < 6; p++) {
            if (s_prev_keys[p] == k) { found = true; break; }
        }
        if (!found && k >= 4 && k < 232)
            s_fn_key(k, 1, ts);
    }

    memcpy(s_prev_keys, RawKeys, 6);
}

/* ---- Mouse handler ---- */

static unsigned s_prev_mouse_buttons = 0;

static void mouse_status_handler(unsigned nButtons,
                                  int nDX, int nDY, int nWheel,
                                  void * /*arg*/)
{
    unsigned long long ts = (unsigned long long)CTimer::GetClockTicks64();

    if (s_fn_motion && (nDX != 0 || nDY != 0))
        s_fn_motion(nDX, nDY, ts);

    if (s_fn_wheel && nWheel != 0)
        s_fn_wheel(nWheel, ts);

    if (s_fn_button) {
        unsigned changed = s_prev_mouse_buttons ^ nButtons;
        for (int b = 0; b < 3; b++) {
            if (changed & (1u << b))
                s_fn_button((uint8_t)(b + 1), (nButtons & (1u << b)) ? 1 : 0, ts);
        }
    }
    s_prev_mouse_buttons = nButtons;
}

/* ---- Device handles ---- */

static CUSBKeyboardDevice *s_pKeyboard = nullptr;
static CMouseDevice       *s_pMouse    = nullptr;

/* ---- extern "C" interface ---- */

extern "C" {

void circle_input_init(void) {}

void circle_input_update(void)
{
    /* Yield to Circle scheduler so USB tasks can run */
    CScheduler *pScheduler = CScheduler::Get();
    if (pScheduler)
        pScheduler->Yield();
    /* Keyboard/mouse handlers are registered directly from kernel.cpp */
}

void circle_input_pump(fn_key_t          fn_key,
                       fn_mouse_motion_t fn_motion,
                       fn_mouse_button_t fn_button,
                       fn_mouse_wheel_t  fn_wheel)
{
    /* Store callbacks so the USB handlers can call them directly */
    s_fn_key    = fn_key;
    s_fn_motion = fn_motion;
    s_fn_button = fn_button;
    s_fn_wheel  = fn_wheel;

    /* Nothing to drain -- events are injected directly from callbacks */
}

/* Called directly from kernel.cpp USB handlers (no SDL internal headers needed) */
void circle_sdl_send_key(unsigned scancode, int down)
{
    if (s_fn_key)
        s_fn_key(scancode, down,
                 (unsigned long long)CTimer::GetClockTicks64());
}

void circle_sdl_send_mouse_motion(int dx, int dy)
{
    if (s_fn_motion)
        s_fn_motion(dx, dy,
                    (unsigned long long)CTimer::GetClockTicks64());
}

void circle_sdl_send_mouse_button(unsigned button, int down)
{
    if (s_fn_button)
        s_fn_button(button, down,
                    (unsigned long long)CTimer::GetClockTicks64());
}

void circle_input_quit(void)
{
    s_fn_key    = nullptr;
    s_fn_motion = nullptr;
    s_fn_button = nullptr;
    s_fn_wheel  = nullptr;
    s_pKeyboard = nullptr;
    s_pMouse    = nullptr;
}

} /* extern "C" */
