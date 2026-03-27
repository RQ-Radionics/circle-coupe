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

// ---- Palette: SAM hardware → 32-bit ARGB lookup table ------------------
// With CScreenDevice (32-bit FB), we can't use HW palette. Convert on CPU.

static uint32_t s_palette32[256] = {};
static bool s_pal_inited = false;

static void BuildPalette()
{
    auto hw = IO::Palette();

    for (int i = 0; i < 256; i++)
    {
        auto& c = hw[i % NUM_PALETTE_COLOURS];
        // ARGB format: 0xFFRRGGBB
        s_palette32[i] = (0xFFu << 24) | ((uint32_t)c.red << 16)
                        | ((uint32_t)c.green << 8) | c.blue;
    }
    s_pal_inited = true;
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

        // Scale to 4:3 filling the screen
        int dst_w = (int)fb_h * 4 / 3;
        int dst_h = (int)fb_h;
        if (dst_w > (int)fb_w) {
            dst_w = (int)fb_w;
            dst_h = (int)fb_w * 3 / 4;
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

        // 32-bit blit with dirty-line detection.
        // Convert 8-bit palette indices to 32-bit ARGB via s_palette32[] LUT.
        static uint8_t s_prev_lines[640][640];
        static int s_prev_src_w = 0;
        static int s_prev_src_h = 0;

        if (src_w != s_prev_src_w || src_h != s_prev_src_h) {
            memset(fbuf, 0, fb_h * pitch);
            memset(s_prev_lines[0], 0xFF, src_h * sizeof(s_prev_lines[0]));
            s_prev_src_w = src_w;
        }

        if (src_h > s_prev_src_h) {
            memset(s_prev_lines[s_prev_src_h], 0xFF,
                   (src_h - s_prev_src_h) * sizeof(s_prev_lines[0]));
        }
        s_prev_src_h = src_h;
        uint8_t *fb8 = (uint8_t *)fbuf;

        for (int sy = 0; sy < src_h; sy++)
        {
            auto pLine = fb.GetLine(sy);

            bool dirty = (memcmp(s_prev_lines[sy], pLine, src_w) != 0);
            if (!dirty) continue;

            memcpy(s_prev_lines[sy], pLine, src_w);

            for (int dy = sy * dst_h / src_h;
                 dy < (sy + 1) * dst_h / src_h && dy < dst_h;
                 dy++)
            {
                uint32_t *row = (uint32_t *)(fb8 + (off_y + dy) * pitch) + off_x;
                for (int dx = 0; dx < dst_w; dx++)
                    row[dx] = s_palette32[pLine[s_xtab[dx]]];
            }
        }

        // No flip needed — single buffer mode.
        // Dirty-line writes go directly to the visible framebuffer.
        // No double-buffer = no parpadeo from stale back buffer data.
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
