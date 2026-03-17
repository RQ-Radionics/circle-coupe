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

/* Flip: wait for vsync, then swap front/back */
void circle_fb_flip(void)
{
    if (!s_pFrameBuffer) return;

    /* Show the back buffer */
    unsigned display_y = s_back_buffer * s_height;
    s_pFrameBuffer->WaitForVerticalSync();
    s_pFrameBuffer->SetVirtualOffset(0, display_y);

    /* Swap */
    s_back_buffer ^= 1;
}

void circle_fb_update(void)
{
    circle_fb_flip();
}

} /* extern "C" */
