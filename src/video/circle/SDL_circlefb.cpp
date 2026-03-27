/*
 * src/video/circle/SDL_circlefb.cpp
 *
 * Double-buffered Circle framebuffer backend for SDL3.
 *
 * CBcmFrameBuffer is created with bDoubleBuffered=TRUE which allocates
 * a virtual framebuffer twice the physical height. We render to the
 * back buffer and flip via SetVirtualOffset on vsync.
 */

#include <stdint.h>
#include <circle/bcmframebuffer.h>
#include <circle/screen.h>

/* Microseconds since boot - reads CNTPCT directly, same fix as SDL_circletimer_impl.cpp */
static inline unsigned long long circle_get_clock_ticks64_us(void)
{
#if AARCH == 32
    unsigned long nLow, nHigh;
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(nLow), "=r"(nHigh));
    unsigned long long cntpct = ((unsigned long long)nHigh << 32) | nLow;
    unsigned long cntfrq;
    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(cntfrq));
    return cntpct * 1000000ULL / (unsigned long long)cntfrq;
#else
    return (unsigned long long)CTimer::GetClockTicks64();
#endif
}

static CScreenDevice   *s_pScreen      = nullptr;
static CBcmFrameBuffer *s_pFrameBuffer = nullptr;  // owned by CScreenDevice
static unsigned s_back_buffer = 0;  /* 0 or 1 -- which buffer is the back */

/* Physical dimensions */
static unsigned s_width  = 0;
static unsigned s_height = 0;
static unsigned s_pitch  = 0;

extern "C" {

int circle_fb_init(unsigned w, unsigned h, unsigned depth)
{
    if (s_pScreen) {
        delete s_pScreen;
        s_pScreen = nullptr;
    }
    if (s_pFrameBuffer) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
    }

    /* CScreenDevice creates framebuffer that survives VCHIQ init on Pi 4.
     * We use its framebuffer directly (32-bit) — no separate 8-bit buffer. */
    (void)depth;  // ignored — CScreenDevice uses DEPTH (32)
    s_pScreen = new CScreenDevice(w, h);
    if (!s_pScreen->Initialize()) {
        delete s_pScreen;
        s_pScreen = nullptr;
        return -1;
    }

    s_pFrameBuffer = s_pScreen->GetFrameBuffer();

    s_width  = s_pFrameBuffer->GetWidth();
    s_height = s_pFrameBuffer->GetHeight();
    s_pitch  = s_pFrameBuffer->GetPitch();
    s_back_buffer = 0;

    return 0;
}

void circle_fb_quit(void)
{
    s_pFrameBuffer = nullptr;  // owned by CScreenDevice
    if (s_pScreen) {
        delete s_pScreen;
        s_pScreen = nullptr;
    }
    s_width = s_height = s_pitch = 0;
}

unsigned circle_fb_get_width(void)  { return s_width; }
unsigned circle_fb_get_height(void) { return s_height; }
unsigned circle_fb_get_pitch(void)  { return s_pitch; }

unsigned circle_fb_get_depth(void)
{
    return s_pFrameBuffer ? s_pFrameBuffer->GetDepth() : 0;
}

void *circle_fb_get_screen_device(void)
{
    return s_pScreen;
}

/* Returns pointer to the BACK buffer (the one we draw into) */
void *circle_fb_get_buffer(void)
{
    if (!s_pFrameBuffer) return nullptr;
    uint8_t *base = reinterpret_cast<uint8_t *>(
        static_cast<uintptr_t>(s_pFrameBuffer->GetBuffer()));
    /* back_buffer=0 -> top half, back_buffer=1 -> bottom half */
    return base + s_back_buffer * s_height * s_pitch;
}

/* Flip: show the back buffer immediately, no waiting.
 * Throttle is done in Frame::Sync() using circle_get_clock_ticks64().
 * SetVirtualOffset takes effect on the next natural vsync from the GPU.
 */
void circle_fb_flip(void)
{
    if (!s_pFrameBuffer) return;
    unsigned display_y = s_back_buffer * s_height;
    s_pFrameBuffer->SetVirtualOffset(0, display_y);
    s_back_buffer ^= 1;
}

/* Flip without blocking on vsync.
 *
 * Used when the GUI/debugger is active on bare-metal.  WaitForVerticalSync()
 * holds the sole RPi3 core for ~20ms, during which Circle's scheduler cannot
 * run USB callbacks → keyboard input is lost and the debugger appears frozen.
 * Without the wait the flip still takes effect on the next natural vsync;
 * occasional tearing in the debugger UI is acceptable.
 */
void circle_fb_flip_nowait(void)
{
    if (!s_pFrameBuffer) return;
    unsigned display_y = s_back_buffer * s_height;
    s_pFrameBuffer->SetVirtualOffset(0, display_y);
    s_back_buffer ^= 1;
}

void circle_fb_update(void)
{
    circle_fb_flip();
}

/* Set a single palette entry (8-bit mode only) */
void circle_fb_set_palette(unsigned index, unsigned r, unsigned g, unsigned b)
{
    if (!s_pFrameBuffer || index >= 256) return;
    // RPi firmware palette format is 0xAABBGGRR (ABGR)
    u32 abgr = (0xFFu << 24) | (b << 16) | (g << 8) | r;
    s_pFrameBuffer->SetPalette32((u8)index, abgr);
}

/* Apply all palette changes to the hardware */
void circle_fb_update_palette(void)
{
    if (s_pFrameBuffer)
        s_pFrameBuffer->UpdatePalette();
}

} /* extern "C" */
