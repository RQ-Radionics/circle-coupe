// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: Circle bare-metal video — direct framebuffer, no SDL
//
// IVideoBase implementation that blits the SAM framebuffer directly
// into CBcmFrameBuffer (double-buffered, 32bpp XRGB8888).
//
// circle_fb_* lives in src/video/circle/SDL_circlefb.cpp (reused as-is).

#include "SimCoupe.h"
#include "Video.h"
#include "Frame.h"
#include "GUI.h"
#include "Options.h"
#include "SAMIO.h"

// circle_fb API (C linkage, from SDL_circlefb.cpp)
extern "C" {
    int      circle_fb_init(unsigned w, unsigned h, unsigned depth);
    void     circle_fb_quit(void);
    unsigned circle_fb_get_width(void);
    unsigned circle_fb_get_height(void);
    unsigned circle_fb_get_pitch(void);
    void    *circle_fb_get_buffer(void);
    void     circle_fb_flip(void);
    void     circle_fb_flip_nowait(void);
    unsigned long long circle_get_clock_ticks64(void);
}

// ---- Palette (SAM hardware → XRGB8888) ----------------------------------
// Built each frame from IO::Palette(). Uses 256 entries (indices 128-255
// mirror 0-127) because FrameBuffer pixel values can exceed NUM_PALETTE_COLOURS.

static uint32_t s_palette[256];

static void BuildPalette()
{
    auto hw = IO::Palette();
    for (int i = 0; i < 256; i++)
    {
        auto& c = hw[i % NUM_PALETTE_COLOURS];
        s_palette[i] = (0xFFu << 24) |
                       ((uint32_t)c.red   << 16) |
                       ((uint32_t)c.green <<  8) |
                        (uint32_t)c.blue;
    }
}

// ---- CircleVideo: IVideoBase implementation ------------------------------

class CircleVideo : public IVideoBase
{
public:
    CircleVideo() = default;
    ~CircleVideo() override { circle_fb_quit(); }

    bool Init() override
    {
        // Palette built per-frame in Update() from IO::Palette()

        // Framebuffer already initialised from kernel.cpp (core 0).
        // Just verify it's up.
        if (circle_fb_get_width() == 0)
        {
            // Fallback init in case kernel didn't do it
            if (circle_fb_init(1280, 720, 32) != 0)
                return false;
        }

        m_fb_w = circle_fb_get_width();
        m_fb_h = circle_fb_get_height();
        return true;
    }

    Rect DisplayRect() const override
    {
        return { 0, 0, (int)m_fb_w, (int)m_fb_h };
    }

    void ResizeWindow(int /*height*/) const override {}

    std::pair<int, int> MouseRelative() override { return { 0, 0 }; }

    void OptionsChanged() override {}

    void Update(const FrameBuffer& fb) override
    {
        // Rebuild palette each frame from SAM hardware state
        BuildPalette();

        unsigned fb_w = m_fb_w;
        unsigned fb_h = m_fb_h;
        if (fb_w == 0 || fb_h == 0) return;

        void    *fbuf    = circle_fb_get_buffer();
        unsigned pitch   = circle_fb_get_pitch();
        if (!fbuf) return;

        int src_w = fb.Width();
        int src_h = fb.Height();
        bool is_gui = GUI::IsActive();

        // Scale to fill framebuffer maintaining 4:3 aspect ratio.
        //
        // SAM source: 512×192 (emulator) or 512×384 (GUI, already double-height)
        // Target: 4:3 window inside fb_w×fb_h
        //   max height = fb_h → width = fb_h * 4/3
        //   if width > fb_w → clamp to fb_w, height = fb_w * 3/4
        //
        // For the GUI (src already double-height), source is treated as
        // a 4:3 image and scaled the same way.
        //
        // Nearest-neighbor: src_x = dst_x * src_w / dst_w
        //                   src_y = dst_y * src_h / dst_h

        int dst_w = (int)fb_h * 4 / 3;
        int dst_h = (int)fb_h;
        if (dst_w > (int)fb_w) {
            dst_w = (int)fb_w;
            dst_h = (int)fb_w * 3 / 4;
        }

        int off_x = ((int)fb_w - dst_w) / 2;
        int off_y = ((int)fb_h - dst_h) / 2;

        // Clear top/bottom border rows
        auto clear_row = [&](unsigned y) {
            uint32_t *row = (uint32_t *)((uint8_t *)fbuf + y * pitch);
            for (unsigned x = 0; x < fb_w; x++) row[x] = 0;
        };
        for (int y = 0; y < off_y; y++) clear_row((unsigned)y);
        for (int y = off_y + dst_h; y < (int)fb_h; y++) clear_row((unsigned)y);

        // Blit with nearest-neighbor scaling, clearing left/right borders inline
        for (int dy = 0; dy < dst_h; dy++)
        {
            int sy = dy * src_h / dst_h;
            auto pLine = fb.GetLine(sy);
            uint32_t *row = (uint32_t *)((uint8_t *)fbuf + (off_y + dy) * pitch);

            // Left border
            for (int x = 0; x < off_x; x++) row[x] = 0;

            // Scaled image
            uint32_t *dst = row + off_x;
            for (int dx = 0; dx < dst_w; dx++) {
                int sx = dx * src_w / dst_w;
                dst[dx] = s_palette[pLine[sx]];
            }

            // Right border
            for (int x = off_x + dst_w; x < (int)fb_w; x++) row[x] = 0;
        }

        if (is_gui)
            circle_fb_flip_nowait();
        else
            circle_fb_flip();
    }

private:
    unsigned m_fb_w = 0;
    unsigned m_fb_h = 0;
};

// Factory function called from UI::CreateVideo()
std::unique_ptr<IVideoBase> CreateCircleVideo()
{
    return std::make_unique<CircleVideo>();
}
