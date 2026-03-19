// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: Circle bare-metal video — 8-bit framebuffer with HW palette
//
// Uses CBcmFrameBuffer in 8-bit indexed mode. The GPU converts palette
// indices to RGB automatically — the ARM only writes 1 byte per pixel.
// circle_fb_* lives in src/video/circle/SDL_circlefb.cpp.

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
    void     circle_fb_set_palette(unsigned index, unsigned r, unsigned g, unsigned b);
    void     circle_fb_update_palette(void);
    unsigned long long circle_get_clock_ticks64(void);
}

// ---- Palette: SAM hardware → GPU hardware palette -----------------------

static void BuildPalette()
{
    auto hw = IO::Palette();
    for (int i = 0; i < 256; i++)
    {
        auto& c = hw[i % NUM_PALETTE_COLOURS];
        circle_fb_set_palette(i, c.red, c.green, c.blue);
    }
    circle_fb_update_palette();
}

// ---- CircleVideo: IVideoBase implementation -----------------------------

class CircleVideo : public IVideoBase
{
public:
    CircleVideo() = default;
    ~CircleVideo() override { circle_fb_quit(); }

    bool Init() override
    {
        if (circle_fb_get_width() == 0)
        {
            if (circle_fb_init(1280, 720, 8) != 0)
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
        unsigned fb_w = m_fb_w;
        unsigned fb_h = m_fb_h;
        if (fb_w == 0 || fb_h == 0) return;

        // Update GPU palette from SAM hardware state
        BuildPalette();

        void    *fbuf  = circle_fb_get_buffer();
        unsigned pitch = circle_fb_get_pitch();
        if (!fbuf) return;

        int src_w = fb.Width();
        int src_h = fb.Height();
        bool is_gui = GUI::IsActive();

        // Scale to 4:3 with 5% safe margin for TV overscan
        int safe_w = (int)fb_w * 90 / 100;
        int safe_h = (int)fb_h * 90 / 100;
        int dst_w = safe_h * 4 / 3;
        int dst_h = safe_h;
        if (dst_w > safe_w) {
            dst_w = safe_w;
            dst_h = safe_w * 3 / 4;
        }

        int off_x = ((int)fb_w - dst_w) / 2;
        int off_y = ((int)fb_h - dst_h) / 2;

        // Precalculate X mapping table (avoids per-pixel division)
        static int s_xtab[1920];
        static int s_xtab_w = 0, s_xtab_sw = 0;
        if (s_xtab_w != dst_w || s_xtab_sw != src_w) {
            for (int dx = 0; dx < dst_w && dx < 1920; dx++)
                s_xtab[dx] = dx * src_w / dst_w;
            s_xtab_w = dst_w;
            s_xtab_sw = src_w;
        }

        // Clear borders once
        static bool s_borders_cleared = false;
        if (!s_borders_cleared) {
            memset(fbuf, 0, fb_h * pitch);
            s_borders_cleared = true;
        }

        // 8-bit blit: write palette indices (1 byte/pixel)
        uint8_t *fb8 = (uint8_t *)fbuf;
        for (int dy = 0; dy < dst_h; dy++)
        {
            int sy = dy * src_h / dst_h;
            auto pLine = fb.GetLine(sy);
            uint8_t *row = fb8 + (off_y + dy) * pitch + off_x;
            for (int dx = 0; dx < dst_w; dx++)
                row[dx] = pLine[s_xtab[dx]];
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
