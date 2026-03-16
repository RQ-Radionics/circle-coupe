/*
 * src/events/circle/SDL_circleevents_impl.cpp
 *
 * C++ glue: hooks Circle USB keyboard and mouse into the SDL3 event queue.
 *
 * Design:
 *   - Raw keyboard mode: TKeyStatusHandlerRawEx callback fired from USB IRQ context.
 *     We diff the previous report vs the current one to detect key down/up events.
 *     USB HID Usage IDs == SDL_Scancode values (USB HID spec, SDL3 was designed this way).
 *   - Modifier keys (bits in ucModifiers) are mapped to their SDL scancodes.
 *   - Mouse: TMouseStatusHandlerEx callback with relative displacement.
 *
 *   Callbacks are called at IRQ level in Circle, so we store events in a small
 *   ring buffer and drain it from PumpEvents (task level).
 */

#include <stdint.h>
#include <string.h>

#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/logger.h>
#include <circle/timer.h>

/* ---- Event ring buffer (IRQ-safe single-producer, single-consumer) ---- */

#define CIRCLE_EVT_RING_SIZE    256

enum CircleEvtType {
    CIRCLE_EVT_NONE = 0,
    CIRCLE_EVT_KEY_DOWN,
    CIRCLE_EVT_KEY_UP,
    CIRCLE_EVT_MOUSE_MOTION,
    CIRCLE_EVT_MOUSE_BUTTON_DOWN,
    CIRCLE_EVT_MOUSE_BUTTON_UP,
    CIRCLE_EVT_MOUSE_WHEEL,
};

struct CircleEvt {
    CircleEvtType type;
    union {
        struct { uint8_t scancode; } key;
        struct { int dx; int dy; } motion;
        struct { uint8_t button; } button;
        struct { int dy; } wheel;
    };
};

static volatile CircleEvt s_ring[CIRCLE_EVT_RING_SIZE];
static volatile unsigned  s_ring_head = 0;  /* producer (IRQ) writes here */
static volatile unsigned  s_ring_tail = 0;  /* consumer (task) reads here */

static void ring_push(const CircleEvt &evt)
{
    unsigned next = (s_ring_head + 1) % CIRCLE_EVT_RING_SIZE;
    if (next == s_ring_tail) return; /* full, drop */
    /* Use memcpy to write through volatile correctly */
    memcpy((void *)&s_ring[s_ring_head], &evt, sizeof(CircleEvt));
    __sync_synchronize();
    s_ring_head = next;
}

static bool ring_pop(CircleEvt &evt)
{
    if (s_ring_tail == s_ring_head) return false;
    __sync_synchronize();
    memcpy(&evt, (const void *)&s_ring[s_ring_tail], sizeof(CircleEvt));
    s_ring_tail = (s_ring_tail + 1) % CIRCLE_EVT_RING_SIZE;
    return true;
}

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
    CircleEvt evt;

    /* --- Modifier changes --- */
    uint8_t mod_changed = s_prev_modifiers ^ ucModifiers;
    for (int i = 0; s_mod_map[i].mask; i++) {
        if (mod_changed & s_mod_map[i].mask) {
            evt.type = (ucModifiers & s_mod_map[i].mask)
                       ? CIRCLE_EVT_KEY_DOWN : CIRCLE_EVT_KEY_UP;
            evt.key.scancode = s_mod_map[i].scancode;
            ring_push(evt);
        }
    }
    s_prev_modifiers = ucModifiers;

    /* --- Key releases: keys in prev but not in current --- */
    for (int p = 0; p < 6; p++) {
        uint8_t k = s_prev_keys[p];
        if (k == 0) continue;
        bool found = false;
        for (int c = 0; c < 6; c++) {
            if (RawKeys[c] == k) { found = true; break; }
        }
        if (!found && k >= 4 && k < 232) {
            evt.type = CIRCLE_EVT_KEY_UP;
            evt.key.scancode = k;
            ring_push(evt);
        }
    }

    /* --- Key presses: keys in current but not in prev --- */
    for (int c = 0; c < 6; c++) {
        uint8_t k = RawKeys[c];
        if (k == 0) continue;
        bool found = false;
        for (int p = 0; p < 6; p++) {
            if (s_prev_keys[p] == k) { found = true; break; }
        }
        if (!found && k >= 4 && k < 232) {
            evt.type = CIRCLE_EVT_KEY_DOWN;
            evt.key.scancode = k;
            ring_push(evt);
        }
    }

    memcpy((void*)s_prev_keys, RawKeys, 6);
}

/* ---- Mouse handler ---- */

static unsigned s_prev_mouse_buttons = 0;

static void mouse_status_handler(unsigned nButtons,
                                  int nDX, int nDY, int nWheel,
                                  void * /*arg*/)
{
    CircleEvt evt;

    /* Motion */
    if (nDX != 0 || nDY != 0) {
        evt.type      = CIRCLE_EVT_MOUSE_MOTION;
        evt.motion.dx = nDX;
        evt.motion.dy = nDY;
        ring_push(evt);
    }

    /* Wheel */
    if (nWheel != 0) {
        evt.type     = CIRCLE_EVT_MOUSE_WHEEL;
        evt.wheel.dy = nWheel;
        ring_push(evt);
    }

    /* Button changes (up to 3 buttons: 1=left, 2=right, 3=middle) */
    unsigned changed = s_prev_mouse_buttons ^ nButtons;
    for (int b = 0; b < 3; b++) {
        if (changed & (1u << b)) {
            evt.type          = (nButtons & (1u << b))
                                ? CIRCLE_EVT_MOUSE_BUTTON_DOWN
                                : CIRCLE_EVT_MOUSE_BUTTON_UP;
            evt.button.button = (uint8_t)(b + 1); /* SDL: 1=left, 2=middle, 3=right */
            ring_push(evt);
        }
    }
    s_prev_mouse_buttons = nButtons;
}

/* ---- Device handles ---- */

static CUSBKeyboardDevice *s_pKeyboard = nullptr;
static CMouseDevice       *s_pMouse    = nullptr;

/* ---- extern "C" interface ---- */

extern "C" {

void circle_input_init(void)
{
    /* Devices may not be available yet at init time (USB plug-and-play).
     * circle_input_update() retries on each PumpEvents call. */
}

void circle_input_update(void)
{
    /* Try to acquire keyboard if not yet attached */
    if (s_pKeyboard == nullptr) {
        s_pKeyboard = (CUSBKeyboardDevice *)
            CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);
        if (s_pKeyboard != nullptr) {
            s_pKeyboard->RegisterKeyStatusHandlerRaw(
                key_status_handler, TRUE /* mixed mode */, nullptr);
        }
    }

    /* Try to acquire mouse if not yet attached */
    if (s_pMouse == nullptr) {
        s_pMouse = (CMouseDevice *)
            CDeviceNameService::Get()->GetDevice("mouse1", FALSE);
        if (s_pMouse != nullptr) {
            s_pMouse->RegisterStatusHandler(mouse_status_handler, nullptr);
        }
    }
}

/* Drain the ring buffer and call the SDL3 event injection callbacks.
 * inject_key(scancode, down, ts), inject_mouse_motion(dx,dy,ts),
 * inject_mouse_button(btn,down,ts), inject_mouse_wheel(dy,ts)
 * are provided as function pointers to avoid a C++ -> C header dependency. */
typedef void (*fn_key_t)          (unsigned scancode, int down, unsigned long long ts);
typedef void (*fn_mouse_motion_t) (int dx, int dy, unsigned long long ts);
typedef void (*fn_mouse_button_t) (unsigned button, int down, unsigned long long ts);
typedef void (*fn_mouse_wheel_t)  (int dy, unsigned long long ts);

void circle_input_pump(fn_key_t          fn_key,
                       fn_mouse_motion_t fn_motion,
                       fn_mouse_button_t fn_button,
                       fn_mouse_wheel_t  fn_wheel)
{
    unsigned long long ts = (unsigned long long)CTimer::GetClockTicks64();
    CircleEvt evt;

    while (ring_pop(evt)) {
        switch (evt.type) {
        case CIRCLE_EVT_KEY_DOWN:
            if (fn_key) fn_key(evt.key.scancode, 1, ts);
            break;
        case CIRCLE_EVT_KEY_UP:
            if (fn_key) fn_key(evt.key.scancode, 0, ts);
            break;
        case CIRCLE_EVT_MOUSE_MOTION:
            if (fn_motion) fn_motion(evt.motion.dx, evt.motion.dy, ts);
            break;
        case CIRCLE_EVT_MOUSE_BUTTON_DOWN:
            if (fn_button) fn_button(evt.button.button, 1, ts);
            break;
        case CIRCLE_EVT_MOUSE_BUTTON_UP:
            if (fn_button) fn_button(evt.button.button, 0, ts);
            break;
        case CIRCLE_EVT_MOUSE_WHEEL:
            if (fn_wheel) fn_wheel(evt.wheel.dy, ts);
            break;
        default:
            break;
        }
    }
}

void circle_input_quit(void)
{
    s_pKeyboard = nullptr;
    s_pMouse    = nullptr;
}

} /* extern "C" */
