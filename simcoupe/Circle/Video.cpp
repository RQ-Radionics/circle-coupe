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

// circle_fb API and timer (C linkage)
extern "C" {
    unsigned long circle_get_ticks(void);
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
    ~CircleVideo() override {
        circle_fb_quit();
        delete[] m_shadow;
    }

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
        m_shadow_pitch = m_fb_w * sizeof(uint32_t);
        m_shadow = new uint32_t[m_fb_w * m_fb_h];
        if (m_shadow)
            memset(m_shadow, 0, m_fb_w * m_fb_h * sizeof(uint32_t));
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
        static unsigned long s_total_us = 0;
        static int s_count = 0;
        unsigned long t0 = circle_get_ticks();

        // Rebuild palette each frame from SAM hardware state
        BuildPalette();

        unsigned fb_w = m_fb_w;
        unsigned fb_h = m_fb_h;
        if (fb_w == 0 || fb_h == 0) return;

        void    *fbuf    = circle_fb_get_buffer();
        unsigned gpu_pitch = circle_fb_get_pitch();
        if (!fbuf || !m_shadow) return;

        // Use shadow buffer (cached RAM) for all rendering,
        // then memcpy to GPU framebuffer (uncached) at the end.
        uint32_t *shadow = m_shadow;
        unsigned pitch = m_shadow_pitch;

        int src_w = fb.Width();
        int src_h = fb.Height();
        bool is_gui = GUI::IsActive();

        // Scale using integer multiples for speed.
        // SAM: 512×192 → 2x = 1024×384, but 1024 > some fb widths
        //   so use src_w×(src_h*2) = 512×384 (just double scanlines)
        // GUI: 512×384 → 1:1 (already double height)
        // Both centred in fb_w×fb_h.
        int dst_w = src_w;           // 512 (or 544 with border)
        int dst_h = is_gui ? src_h : src_h * 2;  // 384

        int off_x = ((int)fb_w - dst_w) / 2;
        int off_y = ((int)fb_h - dst_h) / 2;

        // Clear top/bottom border rows
        auto clear_row = [&](unsigned y) {
            uint32_t *row = (uint32_t *)((uint8_t *)fbuf + y * pitch);
            for (unsigned x = 0; x < fb_w; x++) row[x] = 0;
        };
        for (int y = 0; y < off_y; y++) clear_row((unsigned)y);
        for (int y = off_y + dst_h; y < (int)fb_h; y++) clear_row((unsigned)y);

        // Render to shadow buffer (cached RAM) — fast
        if (is_gui) {
            for (int y = 0; y < dst_h && (off_y + y) < (int)fb_h; y++) {
                auto pLine = fb.GetLine(y);
                uint32_t *row = shadow + (off_y + y) * m_fb_w;
                for (int x = 0; x < off_x; x++) row[x] = 0;
                uint32_t *d = row + off_x;
                for (int x = 0; x < dst_w; x++) d[x] = s_palette[pLine[x]];
                for (int x = off_x + dst_w; x < (int)fb_w; x++) row[x] = 0;
            }
        } else {
            for (int y = 0; y < src_h && (off_y + y*2 + 1) < (int)fb_h; y++) {
                auto pLine = fb.GetLine(y);
                uint32_t *row0 = shadow + (off_y + y*2    ) * m_fb_w;
                uint32_t *row1 = shadow + (off_y + y*2 + 1) * m_fb_w;
                for (int x = 0; x < off_x; x++) { row0[x] = 0; row1[x] = 0; }
                uint32_t *d0 = row0 + off_x;
                uint32_t *d1 = row1 + off_x;
                for (int x = 0; x < dst_w; x++) {
                    uint32_t c = s_palette[pLine[x]];
                    d0[x] = c; d1[x] = c;
                }
                for (int x = off_x + dst_w; x < (int)fb_w; x++) { row0[x] = 0; row1[x] = 0; }
            }
        }

        // Copy shadow → GPU framebuffer in one burst (uncached writes)
        uint8_t *dst_fb = (uint8_t *)fbuf;
        uint8_t *src_sh = (uint8_t *)shadow;
        for (unsigned y = 0; y < fb_h; y++) {
            memcpy(dst_fb + y * gpu_pitch, src_sh + y * pitch, pitch);
        }

        // Measure blit time
        unsigned long t1 = circle_get_ticks();
        s_total_us += (t1 - t0);
        s_count++;
        if (s_count >= 50) {
            extern unsigned long g_blit_avg_us;
            g_blit_avg_us = s_total_us / s_count;
            s_total_us = 0;
            s_count = 0;
        }

        if (is_gui)
            circle_fb_flip_nowait();
        else
            circle_fb_flip();
    }

private:
    unsigned m_fb_w = 0;
    unsigned m_fb_h = 0;
    uint32_t *m_shadow = nullptr;  // cached RAM shadow of framebuffer
    unsigned m_shadow_pitch = 0;
};

// Factory function called from UI::CreateVideo()
std::unique_ptr<IVideoBase> CreateCircleVideo()
{
    return std::make_unique<CircleVideo>();
}
