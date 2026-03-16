/*
 * src/video/circle/SDL_circlefb.cpp
 *
 * C++ glue between SDL3 Circle video backend and CBcmFrameBuffer.
 * Exposes extern "C" functions called by SDL_circlevideo.c.
 *
 * Circle CBcmFrameBuffer allocates its buffer via the VideoCore GPU mailbox.
 * GetBuffer() returns a physical address that is also bus-accessible ARM memory.
 * On RPi3B with AArch32 this is a direct virtual=physical mapping.
 */

#include <stdint.h>
#include <circle/bcmframebuffer.h>

static CBcmFrameBuffer *s_pFrameBuffer = nullptr;

extern "C" {

int circle_fb_init(unsigned w, unsigned h, unsigned depth)
{
    if (s_pFrameBuffer) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
    }

    s_pFrameBuffer = new CBcmFrameBuffer(w, h, depth,
                                          w, h,   /* virtual = physical */
                                          0,       /* display 0 (HDMI) */
                                          FALSE);  /* no double buffering yet */
    if (!s_pFrameBuffer->Initialize()) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
        return -1;
    }

    return 0;
}

void circle_fb_quit(void)
{
    if (s_pFrameBuffer) {
        delete s_pFrameBuffer;
        s_pFrameBuffer = nullptr;
    }
}

unsigned circle_fb_get_width(void)
{
    return s_pFrameBuffer ? s_pFrameBuffer->GetWidth() : 0;
}

unsigned circle_fb_get_height(void)
{
    return s_pFrameBuffer ? s_pFrameBuffer->GetHeight() : 0;
}

unsigned circle_fb_get_pitch(void)
{
    return s_pFrameBuffer ? s_pFrameBuffer->GetPitch() : 0;
}

unsigned circle_fb_get_depth(void)
{
    return s_pFrameBuffer ? s_pFrameBuffer->GetDepth() : 0;
}

void *circle_fb_get_buffer(void)
{
    if (!s_pFrameBuffer) {
        return nullptr;
    }
    /* GetBuffer() returns the physical/bus address of the framebuffer.
     * On RPi 3B AArch32 with identity mapping this is the same as the
     * virtual address we can write to directly. */
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(s_pFrameBuffer->GetBuffer()));
}

void circle_fb_update(void)
{
    /* Single-buffered: nothing to swap.
     * When double-buffering is added, call SetVirtualOffset here. */
}

} /* extern "C" */
