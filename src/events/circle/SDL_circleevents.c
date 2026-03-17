/*
 * src/events/circle/SDL_circleevents.c
 *
 * SDL3 event pump for Circle bare-metal.
 * Called from CIRCLE_PumpEvents() in SDL_circlevideo.c.
 *
 * This file is pure C and links against SDL3 internals.
 * The Circle C++ glue lives in SDL_circleevents_impl.cpp.
 */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_CIRCLE

#include "events/SDL_events_c.h"
#include "events/SDL_keyboard_c.h"
#include "events/SDL_mouse_c.h"

/* Declared in SDL_circleevents_impl.cpp as extern "C" */
typedef void (*fn_key_t)          (unsigned scancode, int down, unsigned long long ts);
typedef void (*fn_mouse_motion_t) (int dx, int dy, unsigned long long ts);
typedef void (*fn_mouse_button_t) (unsigned button, int down, unsigned long long ts);
typedef void (*fn_mouse_wheel_t)  (int dy, unsigned long long ts);

extern void circle_input_init(void);
extern void circle_input_update(void);
extern void circle_input_pump(fn_key_t, fn_mouse_motion_t, fn_mouse_button_t, fn_mouse_wheel_t);
extern void circle_input_quit(void);

/* ---- SDL3 event injection callbacks ---- */

static void inject_key(unsigned scancode, int down, unsigned long long ts)
{
    SDL_SendKeyboardKey((Uint64)ts, SDL_GLOBAL_KEYBOARD_ID,
                        (int)scancode, (SDL_Scancode)scancode, (bool)down);
}

static void inject_mouse_motion(int dx, int dy, unsigned long long ts)
{
    SDL_Window *window = SDL_GetMouseFocus();
    SDL_SendMouseMotion((Uint64)ts, window, SDL_GLOBAL_MOUSE_ID,
                        true /* relative */, (float)dx, (float)dy);
}

static void inject_mouse_button(unsigned button, int down, unsigned long long ts)
{
    SDL_Window *window = SDL_GetMouseFocus();
    SDL_SendMouseButton((Uint64)ts, window, SDL_GLOBAL_MOUSE_ID,
                        (Uint8)button, (bool)down);
}

static void inject_mouse_wheel(int dy, unsigned long long ts)
{
    SDL_Window *window = SDL_GetMouseFocus();
    SDL_SendMouseWheel((Uint64)ts, window, SDL_GLOBAL_MOUSE_ID,
                       0.0f, (float)dy, SDL_MOUSEWHEEL_NORMAL);
}

/* ---- Public API called from SDL_circlevideo.c ---- */

void SDL_CIRCLE_InitEvents(void)
{
    /* Set callbacks immediately so kernel USB handler can use them right away */
    circle_input_pump(inject_key,
                      inject_mouse_motion,
                      inject_mouse_button,
                      inject_mouse_wheel);
    circle_input_init();
}

void SDL_CIRCLE_QuitEvents(void)
{
    circle_input_quit();
}

void SDL_CIRCLE_PumpEvents(void)
{
    /* Poll for newly attached USB devices (plug-and-play) */
    circle_input_update();

    /* Drain the IRQ-level event ring into SDL */
    circle_input_pump(inject_key,
                      inject_mouse_motion,
                      inject_mouse_button,
                      inject_mouse_wheel);

#ifdef SDL_VIDEO_DRIVER_CIRCLE
#endif
}

#endif /* SDL_VIDEO_DRIVER_CIRCLE */
