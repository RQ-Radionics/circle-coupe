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

static CBcmFrameBuffer *s_pFrameBuffer = nullptr;
static unsigned s_back_buffer = 0;  /* 0 or 1 -- which buffer is the back */

/* Physical dimensions */
static unsigned s_width  = 0;
static unsigned s_height = 0;
static unsigned s_pitch  = 0;

extern "C" {

int circle_fb_init(unsigned w, unsigned h, unsigned depth)
{
    if (s_pFrameBuffer) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
    }

    /* Double-buffered: virtual height = physical height * 2 */
    s_pFrameBuffer = new CBcmFrameBuffer(w, h, depth,
                                          w, h * 2,   /* virtual double height */
                                          0,           /* HDMI display */
                                          TRUE);       /* double buffered */
    if (!s_pFrameBuffer->Initialize()) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
        return -1;
    }

    s_width  = s_pFrameBuffer->GetWidth();
    s_height = s_pFrameBuffer->GetHeight();
    s_pitch  = s_pFrameBuffer->GetPitch();
    s_back_buffer = 1;  /* start rendering to buffer 1, display shows buffer 0 */

    /* Show buffer 0 initially */
    s_pFrameBuffer->SetVirtualOffset(0, 0);

    return 0;
}

void circle_fb_quit(void)
{
    if (s_pFrameBuffer) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
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

/* Returns pointer to the BACK buffer (the one we draw into) */
void *circle_fb_get_buffer(void)
{
    if (!s_pFrameBuffer) return nullptr;
    uint8_t *base = reinterpret_cast<uint8_t *>(
        static_cast<uintptr_t>(s_pFrameBuffer->GetBuffer()));
    /* back_buffer=0 -> top half, back_buffer=1 -> bottom half */
    return base + s_back_buffer * s_height * s_pitch;
}

/* Flip: request page flip then wait for vsync to confirm it took effect.
 *
 * Order matters: SetVirtualOffset() queues the flip with the GPU; the
 * VideoCore applies it on the next vertical blank.  WaitForVerticalSync()
 * then blocks until that blank occurs, guaranteeing the new buffer is
 * visible before we start overwriting the old one.  Reversing the order
 * (vsync first, then set offset) causes the SetVirtualOffset to land in
 * the middle of an active scanout period → visible tearing/flicker.
 */
void circle_fb_flip(void)
{
    if (!s_pFrameBuffer) return;

    /* Queue the flip: show the back buffer starting from the next vsync */
    unsigned display_y = s_back_buffer * s_height;
    s_pFrameBuffer->SetVirtualOffset(0, display_y);

    /* Wait until the VideoCore has actually switched to the new buffer */
    s_pFrameBuffer->WaitForVerticalSync();

    /* Now safe to render into the old front buffer (new back buffer) */
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

} /* extern "C" */
