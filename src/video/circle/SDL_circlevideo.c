/*
 * src/video/circle/SDL_circlevideo.c
 *
 * SDL3 video driver for Circle bare-metal (Raspberry Pi 3B AArch32).
 *
 * Implements the SDL3 VideoBootStrap / SDL_VideoDevice interface using
 * Circle's CBcmFrameBuffer as the display surface.
 *
 * Architecture:
 *   - CBcmFrameBuffer allocated at VideoInit with ARGB8888 (32bpp)
 *   - SDL software renderer writes into an SDL_Surface
 *   - UpdateWindowFramebuffer copies the surface to the physical framebuffer
 *   - No OpenGL, no Vulkan, no hardware acceleration
 *
 * The C++ glue to Circle (circle_fb_*) lives in SDL_circlefb.cpp.
 */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_CIRCLE

#include "video/SDL_sysvideo.h"
#include "video/SDL_pixels_c.h"
#include "events/SDL_events_c.h"
#include "SDL_properties_c.h"

#include "SDL_circlevideo.h"

/* Event pump - implemented in src/events/circle/ */
extern void SDL_CIRCLE_InitEvents(void);
extern void SDL_CIRCLE_QuitEvents(void);
extern void SDL_CIRCLE_PumpEvents(void);

/* ---- Resolution defaults ---- */
#ifndef CIRCLE_SCREEN_WIDTH
#define CIRCLE_SCREEN_WIDTH   640
#endif
#ifndef CIRCLE_SCREEN_HEIGHT
#define CIRCLE_SCREEN_HEIGHT  480
#endif
#define CIRCLE_SCREEN_DEPTH   32   /* ARGB8888 */

/* Property key for the SDL_Surface stored per-window */
#define CIRCLE_SURFACE "SDL.internal.window.surface.circle"

/* ------------------------------------------------------------------ */
/* Window framebuffer callbacks                                        */
/* ------------------------------------------------------------------ */

static bool CIRCLE_CreateWindowFramebuffer(SDL_VideoDevice *_this,
                                            SDL_Window *window,
                                            SDL_PixelFormat *format,
                                            void **pixels, int *pitch)
{
    SDL_Surface *surface;
    int w, h;

    /* Use the physical framebuffer size regardless of what the window asked for */
    w = (int)circle_fb_get_width();
    h = (int)circle_fb_get_height();

    /* Use XRGB8888 for the CPU-side surface (alpha ignored on framebuffer) */
    surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        return false;
    }

    SDL_SetSurfaceProperty(SDL_GetWindowProperties(window), CIRCLE_SURFACE, surface);
    *format = SDL_PIXELFORMAT_XRGB8888;
    *pixels = surface->pixels;
    *pitch  = surface->pitch;
    return true;
}

static bool CIRCLE_UpdateWindowFramebuffer(SDL_VideoDevice *_this,
                                            SDL_Window *window,
                                            const SDL_Rect *rects,
                                            int numrects)
{
    SDL_Surface *surface;
    void *fb;
    Uint32 fb_pitch;
    int fb_w, fb_h;

    surface = (SDL_Surface *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), CIRCLE_SURFACE, NULL);
    if (!surface) {
        return SDL_SetError("No Circle framebuffer surface for window");
    }

    fb = circle_fb_get_buffer();
    if (!fb) {
        return SDL_SetError("Circle framebuffer not initialized");
    }

    fb_pitch = circle_fb_get_pitch();
    fb_w = (int)circle_fb_get_width();
    fb_h = (int)circle_fb_get_height();

    /* Copy the SDL surface to the physical framebuffer.
     * Both are XRGB8888; use min dimensions to avoid overrun. */
    {
        const Uint8 *src = (const Uint8 *)surface->pixels;
        Uint8 *dst = (Uint8 *)fb;
        int row;
        int copy_h = surface->h < fb_h ? surface->h : fb_h;
        Uint32 copy_pitch = (Uint32)surface->pitch < fb_pitch
                            ? (Uint32)surface->pitch : fb_pitch;
        for (row = 0; row < copy_h; row++) {
            SDL_memcpy(dst, src, copy_pitch);
            src += surface->pitch;
            dst += fb_pitch;
        }
    }

    return true;
}

static void CIRCLE_DestroyWindowFramebuffer(SDL_VideoDevice *_this,
                                             SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), CIRCLE_SURFACE);
}

/* ------------------------------------------------------------------ */
/* Event pump (no-op; real events come from USB in SDL_circleevents)  */
/* ------------------------------------------------------------------ */

static void CIRCLE_PumpEvents(SDL_VideoDevice *_this)
{
    (void)_this;
    SDL_CIRCLE_PumpEvents();
}

/* ------------------------------------------------------------------ */
/* Window management                                                   */
/* ------------------------------------------------------------------ */

static bool CIRCLE_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_MOVED, 0, 0);
    return true;
}

static void CIRCLE_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    /* Always report the physical framebuffer size */
    int w = (int)circle_fb_get_width();
    int h = (int)circle_fb_get_height();
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
}

static bool CIRCLE_CreateSDLWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    /* Override the requested size to match the physical framebuffer.
     * Set internal fields directly to avoid PIXEL_SIZE_CHANGED which
     * invalidates the window surface before it's even created. */
    int w = (int)circle_fb_get_width();
    int h = (int)circle_fb_get_height();
    window->x = 0;
    window->y = 0;
    window->w = w;
    window->h = h;
    window->last_pixel_w = w;
    window->last_pixel_h = h;

    /* On bare-metal there is only one window -- give it keyboard focus immediately */
    SDL_SetKeyboardFocus(window);

    return true;
}

/* ------------------------------------------------------------------ */
/* VideoInit / VideoQuit                                               */
/* ------------------------------------------------------------------ */

static bool CIRCLE_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    int w, h;

    /* Initialise input event system */
    SDL_CIRCLE_InitEvents();

    /* Initialise the Circle framebuffer */
    w = CIRCLE_SCREEN_WIDTH;
    h = CIRCLE_SCREEN_HEIGHT;
    if (circle_fb_init((unsigned)w, (unsigned)h, CIRCLE_SCREEN_DEPTH) != 0) {
        return SDL_SetError("Circle: CBcmFrameBuffer::Initialize() failed");
    }

    /* Report the actual resolution the firmware negotiated */
    w = (int)circle_fb_get_width();
    h = (int)circle_fb_get_height();

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = w;
    mode.h = h;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }

    return true;
}

static void CIRCLE_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_CIRCLE_QuitEvents();
    circle_fb_quit();
}

/* ------------------------------------------------------------------ */
/* Device creation / bootstrap                                         */
/* ------------------------------------------------------------------ */

static void CIRCLE_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *CIRCLE_CreateDevice(void)
{
    SDL_VideoDevice *device;

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }

    device->VideoInit                  = CIRCLE_VideoInit;
    device->VideoQuit                  = CIRCLE_VideoQuit;
    device->PumpEvents                 = CIRCLE_PumpEvents;
    device->CreateSDLWindow            = CIRCLE_CreateSDLWindow;
    device->SetWindowPosition          = CIRCLE_SetWindowPosition;
    device->SetWindowSize              = CIRCLE_SetWindowSize;
    device->CreateWindowFramebuffer    = CIRCLE_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer    = CIRCLE_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer   = CIRCLE_DestroyWindowFramebuffer;
    device->free                       = CIRCLE_DeleteDevice;

    return device;
}

VideoBootStrap CIRCLE_bootstrap = {
    CIRCLEVID_DRIVER_NAME, "SDL Circle bare-metal video driver",
    CIRCLE_CreateDevice,
    NULL,   /* no ShowMessageBox */
    false   /* not safe for threads */
};

#endif /* SDL_VIDEO_DRIVER_CIRCLE */
