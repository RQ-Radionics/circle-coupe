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

// ---- Palette (SAM 128-colour → XRGB8888) --------------------------------

static uint32_t s_palette[256];

static void BuildPalette()
{
    // SAM Coupe uses a 3-bit RGB palette (8 base colours × brightness)
    // Colour index: bits 7:6=unused 5:4=G 3:2=R 1:0=B (but each 2 bits → 0/1)
    // Actually SimCoupe uses an internal 256-entry palette populated by
    // FrameBuffer::SetPalette. We use the same mapping as SDL20.cpp.
    for (int i = 0; i < 256; i++)
    {
        // SAM palette: IIGRB where I=intensity, G/R/B = colour bits
        // bits 1:0 = blue, bits 3:2 = red, bits 5:4 = green, bit 6 = bright
        int bright = (i & 0x40) ? 255 : 128;
        int r = (i & 0x04) ? bright : 0;
        int g = (i & 0x10) ? bright : 0;
        int b = (i & 0x01) ? bright : 0;
        s_palette[i] = (uint32_t)((r << 16) | (g << 8) | b);
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
        BuildPalette();

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
        unsigned fb_w = m_fb_w;
        unsigned fb_h = m_fb_h;
        if (fb_w == 0 || fb_h == 0) return;

        void    *fbuf    = circle_fb_get_buffer();
        unsigned pitch   = circle_fb_get_pitch();
        if (!fbuf) return;

        int src_w = fb.Width();
        int src_h = fb.Height();
        bool is_gui = GUI::IsActive();

        // Scale factors: GUI already double-height → 1:1, emulator → 2x vertical
        int scale_y = is_gui ? 1 : 2;

        int dst_w = std::min(src_w, (int)fb_w);
        int dst_h = src_h * scale_y;
        if (dst_h > (int)fb_h) dst_h = (int)fb_h;

        int off_x = ((int)fb_w - dst_w) / 2;
        int off_y = ((int)fb_h - dst_h) / 2;

        // Clear top/bottom borders
        auto clear_row = [&](unsigned y) {
            uint32_t *row = (uint32_t *)((uint8_t *)fbuf + y * pitch);
            for (unsigned x = 0; x < fb_w; x++) row[x] = 0;
        };
        for (int y = 0; y < off_y; y++) clear_row((unsigned)y);
        for (int y = off_y + dst_h; y < (int)fb_h; y++) clear_row((unsigned)y);

        if (is_gui)
        {
            for (int y = 0; y < dst_h; y++)
            {
                auto pLine = fb.GetLine(y);
                uint32_t *row = (uint32_t *)((uint8_t *)fbuf + (off_y + y) * pitch);
                for (int x = 0; x < off_x; x++) row[x] = 0;
                uint32_t *dst = row + off_x;
                for (int x = 0; x < dst_w; x++) dst[x] = s_palette[pLine[x]];
                for (int x = off_x + dst_w; x < (int)fb_w; x++) row[x] = 0;
            }
            circle_fb_flip_nowait();
        }
        else
        {
            for (int y = 0; y < src_h && (off_y + y*2 + 1) < (int)fb_h; y++)
            {
                auto pLine = fb.GetLine(y);
                uint32_t *row0 = (uint32_t *)((uint8_t *)fbuf + (off_y + y*2    ) * pitch);
                uint32_t *row1 = (uint32_t *)((uint8_t *)fbuf + (off_y + y*2 + 1) * pitch);
                for (int x = 0; x < off_x; x++) { row0[x] = 0; row1[x] = 0; }
                uint32_t *dst0 = row0 + off_x;
                uint32_t *dst1 = row1 + off_x;
                for (int x = 0; x < dst_w; x++) {
                    uint32_t c = s_palette[pLine[x]];
                    dst0[x] = c; dst1[x] = c;
                }
                for (int x = off_x + dst_w; x < (int)fb_w; x++) { row0[x] = 0; row1[x] = 0; }
            }
            circle_fb_flip();
        }
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
