// Part of SimCoupe - A SAM Coupe emulator
//
// SDL20.cpp: Hardware accelerated textures for SDL 2.0/3.0
//
//  Copyright (c) 1999-2015 Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "SimCoupe.h"
#include "SDL20.h"

#include "Frame.h"
#include "GUI.h"
#include "Options.h"
#include "UI.h"

#if defined(HAVE_LIBSDL2) || defined(HAVE_LIBSDL3)

#ifdef __circle__
extern "C" unsigned circle_fb_get_width(void);
extern "C" unsigned circle_fb_get_height(void);
extern "C" unsigned circle_fb_get_pitch(void);
extern "C" void    *circle_fb_get_buffer(void);
#endif

static uint32_t aulPalette[NUM_PALETTE_COLOURS];

SDLTexture::SDLTexture()
{
    // Disable vsync for as long as we're in the same thread as emulation and sound.
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
}

SDLTexture::~SDLTexture()
{
    SaveWindowPosition();
}

Rect SDLTexture::DisplayRect() const
{
    return { m_rDisplay.x, m_rDisplay.y, m_rDisplay.w, m_rDisplay.h };
}

bool SDLTexture::Init()
{
#ifdef __circle__
    /* On bare-metal, use framebuffer size directly, no hidden/resize */
    int fb_w = (int)circle_fb_get_width();
    int fb_h = (int)circle_fb_get_height();
    m_window = SDL_CreateWindow("SimCoupe/SDL", fb_w, fb_h, 0);
#elif defined(HAVE_LIBSDL3)
    SDL_WindowFlags window_flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
    m_window = SDL_CreateWindow(
        "SimCoupe/SDL",
        Frame::AspectWidth() * 3 / 2, Frame::Height() * 3 / 2, window_flags);
#else
    Uint32 window_flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
    m_window = SDL_CreateWindow(
        "SimCoupe/SDL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        Frame::AspectWidth() * 3 / 2, Frame::Height() * 3 / 2, window_flags);
#endif
    if (!m_window)
        return false;

#ifndef __circle__
    SDL_SetWindowMinimumSize(m_window, Frame::Width() / 2, Frame::Height() / 2);
#endif

#ifdef HAVE_LIBSDL3
    m_renderer.reset(SDL_CreateRenderer(m_window, "software"));
#else
    m_renderer.reset(
        SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE));
#endif

    if (!m_renderer)
        return false;

#ifdef HAVE_LIBSDL3
    auto renderer_name = SDL_GetRendererName(m_renderer);
    auto caption = fmt::format("{} ({})", SDL_GetWindowTitle(m_window), renderer_name ? renderer_name : "unknown");
#else
    SDL_RendererInfo info{ "" };
    SDL_GetRendererInfo(m_renderer, &info);
    auto caption = fmt::format("{} ({})", SDL_GetWindowTitle(m_window), info.name);
#endif

#ifdef _DEBUG
    caption += " [DEBUG]";
#endif
    SDL_SetWindowTitle(m_window, caption.c_str());

    OptionsChanged();
#ifndef __circle__
    RestoreWindowPosition();
    SDL_ShowWindow(m_window);
#endif

    return true;
}


void SDLTexture::OptionsChanged()
{
    uint8_t fill_intensity = GetOption(blackborder) ? 0 : 25;
    SDL_SetRenderDrawColor(m_renderer, fill_intensity, fill_intensity, fill_intensity, 0xff);

    m_rSource.w = m_rSource.h = 0;
    m_rTarget.w = m_rTarget.h = 0;
}

void SDLTexture::Update(const FrameBuffer& fb)
{
#ifdef __circle__
    // Bypass SDL render pipeline -- blit directly to Circle framebuffer.
    // Build XRGB8888 palette directly from IO::Palette() (no texture needed).
    {
        void *fbuf = circle_fb_get_buffer();
        if (!fbuf) return;

        // Build local XRGB8888 palette from SAM hardware palette
        uint32_t palette[NUM_PALETTE_COLOURS];
        auto hw_palette = IO::Palette();
        for (size_t i = 0; i < hw_palette.size(); i++) {
            auto& c = hw_palette[i];
            palette[i] = (0xFF << 24) |
                         ((uint32_t)c.red   << 16) |
                         ((uint32_t)c.green <<  8) |
                          (uint32_t)c.blue;
        }

        unsigned fb_pitch = circle_fb_get_pitch();
        unsigned fb_w = circle_fb_get_width();
        unsigned fb_h = circle_fb_get_height();

        int src_w = fb.Width();
        int src_h = fb.Height();

        // Scale to fill framebuffer, maintaining aspect ratio (2x vertical for SAM 192-line output)
        int dst_h = src_h * 2;
        int dst_w = src_w;
        if (dst_h > (int)fb_h) dst_h = (int)fb_h;
        if (dst_w > (int)fb_w) dst_w = (int)fb_w;

        // Centre on framebuffer
        int off_x = ((int)fb_w - dst_w) / 2;
        int off_y = ((int)fb_h - dst_h) / 2;

        // Clear framebuffer to black
        for (unsigned y = 0; y < fb_h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)fbuf + y * fb_pitch);
            for (unsigned x = 0; x < fb_w; x++)
                row[x] = 0;
        }

        // Blit with vertical 2x scaling
        for (int y = 0; y < src_h && (y * 2 + 1) < (int)fb_h; y++) {
            auto pLine = fb.GetLine(y);
            uint32_t *dst0 = (uint32_t *)((uint8_t *)fbuf + (off_y + y * 2    ) * fb_pitch) + off_x;
            uint32_t *dst1 = (uint32_t *)((uint8_t *)fbuf + (off_y + y * 2 + 1) * fb_pitch) + off_x;
            for (int x = 0; x < dst_w; x++) {
                uint32_t c = palette[pLine[x]];
                dst0[x] = c;
                dst1[x] = c;
            }
        }
    }
    return;
#endif
    if (DrawChanges(fb))
        Render();
}

void SDLTexture::ResizeWindow(int height) const
{
#ifdef HAVE_LIBSDL3
    if (SDL_GetWindowFlags(m_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
        return;
#else
    if (SDL_GetWindowFlags(m_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
        return;
#endif

    auto width = height * Frame::AspectWidth() / Frame::Height();
    SDL_SetWindowSize(m_window, width, height);
}

std::pair<int, int> SDLTexture::MouseRelative()
{
#ifdef HAVE_LIBSDL3
    float mouse_x{}, mouse_y{};
    SDL_GetMouseState(&mouse_x, &mouse_y);

    float centre_x = m_rTarget.w / 2.0f;
    float centre_y = m_rTarget.h / 2.0f;
    auto dx = mouse_x - centre_x;
    auto dy = mouse_y - centre_y;

    auto pix_x = static_cast<float>(m_rDisplay.w) / Frame::Width() * 2;
    auto pix_y = static_cast<float>(m_rDisplay.h) / Frame::Height() * 2;

    auto dx_sam = static_cast<int>(dx / pix_x);
    auto dy_sam = static_cast<int>(dy / pix_y);

    if (dx_sam || dy_sam)
    {
        auto x_remain = std::fmod(dx, pix_x);
        auto y_remain = std::fmod(dy, pix_y);
        SDL_WarpMouseInWindow(nullptr, centre_x + x_remain, centre_y + y_remain);
    }
#else
    SDL_Point mouse{};
    SDL_GetMouseState(&mouse.x, &mouse.y);

    SDL_Point centre{ m_rTarget.w / 2, m_rTarget.h / 2 };
    auto dx = mouse.x - centre.x;
    auto dy = mouse.y - centre.y;

    auto pix_x = static_cast<float>(m_rDisplay.w) / Frame::Width() * 2;
    auto pix_y = static_cast<float>(m_rDisplay.h) / Frame::Height() * 2;

    auto dx_sam = static_cast<int>(dx / pix_x);
    auto dy_sam = static_cast<int>(dy / pix_y);

    if (dx_sam || dy_sam)
    {
        auto x_remain = static_cast<int>(std::fmod(dx, pix_x));
        auto y_remain = static_cast<int>(std::fmod(dy, pix_y));
        SDL_WarpMouseInWindow(nullptr, centre.x + x_remain, centre.y + y_remain);
    }
#endif

    return { dx_sam, dy_sam };
}

void SDLTexture::UpdatePalette()
{
    if (!m_screen_texture)
        return;

    int bpp;
    Uint32 r_mask, g_mask, b_mask, a_mask;

#ifdef HAVE_LIBSDL3
    SDL_PixelFormat format = SDL_GetTextureProperties(m_screen_texture)
        ? static_cast<SDL_PixelFormat>(SDL_GetNumberProperty(
            SDL_GetTextureProperties(m_screen_texture), SDL_PROP_TEXTURE_FORMAT_NUMBER, SDL_PIXELFORMAT_UNKNOWN))
        : SDL_PIXELFORMAT_UNKNOWN;
    SDL_GetMasksForPixelFormat(format, &bpp, &r_mask, &g_mask, &b_mask, &a_mask);
#else
    Uint32 format;
    SDL_QueryTexture(m_screen_texture, &format, nullptr, nullptr, nullptr);
    SDL_PixelFormatEnumToMasks(format, &bpp, &r_mask, &g_mask, &b_mask, &a_mask);
#endif

    auto palette = IO::Palette();
    for (size_t i = 0; i < palette.size(); ++i)
    {
        auto& colour = palette[i];
        aulPalette[i] = RGB2Native(
            colour.red, colour.green, colour.blue, 0xff,
            r_mask, g_mask, b_mask, a_mask);
    }
}

bool SDLTexture::DrawChanges(const FrameBuffer& fb)
{
#ifdef HAVE_LIBSDL3
    bool is_fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (is_fullscreen != GetOption(fullscreen))
    {
        SDL_SetWindowFullscreen(m_window, GetOption(fullscreen));
    }
#else
    bool is_fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (is_fullscreen != GetOption(fullscreen))
    {
        SDL_SetWindowFullscreen(
            m_window,
            GetOption(fullscreen) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
#endif

    int width = fb.Width();
    int height = fb.Height();

    int window_width{};
    int window_height{};
    SDL_GetWindowSize(m_window, &window_width, &window_height);

    bool smooth = !GUI::IsActive() && GetOption(smooth);
    bool smooth_changed = smooth != m_smooth;
    bool source_changed = (width != m_rSource.w) || (height != m_rSource.h);
    bool target_changed = window_width != m_rTarget.w || window_height != m_rTarget.h;

    if (source_changed)
        ResizeSource(width, height);

    if (source_changed || target_changed)
        ResizeTarget(window_width, window_height);

    if (source_changed || target_changed || smooth_changed)
        ResizeIntermediate(smooth);

    if (!m_screen_texture)
        return false;

    int texture_pitch = 0;
    uint8_t* pTexture = nullptr;
    if (SDL_LockTexture(m_screen_texture, nullptr, (void**)&pTexture, &texture_pitch) != 0)
        return false;

    int width_cells = width / GFX_PIXELS_PER_CELL;
    auto pLine = fb.GetLine(0);
    long line_pitch = fb.Width();

    for (int y = 0; y < height; ++y)
    {
        auto pdw = reinterpret_cast<uint32_t*>(pTexture);
        auto pb = pLine;

        for (int x = 0; x < width_cells; ++x)
        {
            for (int i = 0; i < GFX_PIXELS_PER_CELL; ++i)
                pdw[i] = aulPalette[pb[i]];

            pdw += GFX_PIXELS_PER_CELL;
            pb += GFX_PIXELS_PER_CELL;

        }

        pTexture += texture_pitch;
        pLine += line_pitch;
    }

    SDL_UnlockTexture(m_screen_texture);
    return true;
}

void SDLTexture::Render()
{
#ifdef HAVE_LIBSDL3
    float tw{}, th{};
    SDL_GetTextureSize(m_scaled_texture, &tw, &th);
    SDL_FRect rScaledTexture{ 0.0f, 0.0f, tw, th };
#else
    SDL_Rect rScaledTexture{};
    SDL_QueryTexture(m_scaled_texture, nullptr, nullptr, &rScaledTexture.w, &rScaledTexture.h);
#endif

    // Integer scale original image using point sampling.
    SDL_SetRenderTarget(m_renderer, m_scaled_texture);
#ifdef HAVE_LIBSDL3
    SDL_RenderTexture(m_renderer, m_screen_texture, nullptr, nullptr);
#else
    SDL_RenderCopy(m_renderer, m_screen_texture, nullptr, nullptr);
#endif

    if (GetOption(allowmotionblur) && GetOption(motionblur))
    {
        SDL_SetRenderTarget(m_renderer, m_composed_texture);
#ifdef HAVE_LIBSDL3
        SDL_RenderTexture(m_renderer, m_scaled_texture, nullptr, nullptr);
#else
        SDL_RenderCopy(m_renderer, m_scaled_texture, nullptr, nullptr);
#endif

        auto blend_alpha = GetOption(blurpercent) * 0xff / 100;
        SDL_SetTextureAlphaMod(m_prev_composed_texture, blend_alpha);

        if (SDL_SetTextureBlendMode(m_prev_composed_texture, SDL_BLENDMODE_BLEND) == 0)
#ifdef HAVE_LIBSDL3
            SDL_RenderTexture(m_renderer, m_prev_composed_texture, nullptr, nullptr);
#else
            SDL_RenderCopy(m_renderer, m_prev_composed_texture, nullptr, nullptr);
#endif
        SDL_SetTextureBlendMode(m_prev_composed_texture, SDL_BLENDMODE_NONE);
    }
    else
    {
        std::swap(m_composed_texture, m_scaled_texture);
    }

    // Draw to window with remaining scaling using linear sampling.
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_RenderClear(m_renderer);
#ifdef HAVE_LIBSDL3
    SDL_FRect rDisplayF{
        static_cast<float>(m_rDisplay.x), static_cast<float>(m_rDisplay.y),
        static_cast<float>(m_rDisplay.w), static_cast<float>(m_rDisplay.h) };
    SDL_RenderTexture(m_renderer, m_composed_texture, nullptr, &rDisplayF);
#else
    SDL_RenderCopy(m_renderer, m_composed_texture, nullptr, &m_rDisplay);
#endif

    SDL_RenderPresent(m_renderer);
    std::swap(m_composed_texture, m_prev_composed_texture);
}

void SDLTexture::ResizeSource(int source_width, int source_height)
{
#ifdef HAVE_LIBSDL3
    m_screen_texture.reset(
        SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_UNKNOWN,
            SDL_TEXTUREACCESS_STREAMING,
            source_width,
            source_height));

    if (m_screen_texture)
        SDL_SetTextureScaleMode(m_screen_texture, SDL_SCALEMODE_NEAREST);
#else
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    m_screen_texture.reset(
        SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_UNKNOWN,
            SDL_TEXTUREACCESS_STREAMING,
            source_width,
            source_height));
#endif

    UpdatePalette();

    m_rSource.w = source_width;
    m_rSource.h = source_height;
}

void SDLTexture::ResizeTarget(int target_width, int target_height)
{
    auto aspect_ratio = GetOption(tvaspect) ? GFX_DISPLAY_ASPECT_RATIO : 1.0f;
    auto width = static_cast<int>(std::round(Frame::Width() * aspect_ratio));
    auto height = Frame::Height();

    int width_fit = width * target_height / height;
    int height_fit = height * target_width / width;

    if (width_fit <= target_width)
    {
        width = width_fit;
        height = target_height;
    }
    else if (height_fit <= target_height)
    {
        width = target_width;
        height = height_fit;
    }

    m_rDisplay.x = (target_width - width) / 2;
    m_rDisplay.y = (target_height - height) / 2;
    m_rDisplay.w = width;
    m_rDisplay.h = height;

    m_rTarget.w = target_width;
    m_rTarget.h = target_height;
}

void SDLTexture::ResizeIntermediate(bool smooth)
{
    int width_scale = (m_rTarget.w + (m_rSource.w - 1)) / m_rSource.w;
    int height_scale = (m_rTarget.h + (m_rSource.h - 1)) / m_rSource.h;

    if (smooth)
    {
        width_scale = 1;
        height_scale = 2;
    }

    int width = m_rSource.w * width_scale;
    int height = m_rSource.h * height_scale;

    auto create_target_texture = [&]() {
        auto tex = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_UNKNOWN,
            SDL_TEXTUREACCESS_TARGET,
            width,
            height);
#ifdef HAVE_LIBSDL3
        if (tex)
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
#endif
        return tex;
    };

#ifndef HAVE_LIBSDL3
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
#endif

    m_scaled_texture.reset(create_target_texture());
    m_composed_texture.reset(create_target_texture());
    m_prev_composed_texture.reset(create_target_texture());

    m_smooth = smooth;
}

void SDLTexture::SaveWindowPosition()
{
    if (!m_window || !m_rDisplay.w)
        return;

#ifdef HAVE_LIBSDL3
    SDL_SetWindowFullscreen(m_window, false);
#else
    SDL_SetWindowFullscreen(m_window, 0);
#endif

    auto window_flags = SDL_GetWindowFlags(m_window);
    auto maximised = (window_flags & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
    SDL_RestoreWindow(m_window);

    int x, y, width, height;
    SDL_GetWindowPosition(m_window, &x, &y);
    SDL_GetWindowSize(m_window, &width, &height);

    SetOption(windowpos, fmt::format("{},{},{},{},{}", x, y, width, height, maximised).c_str());
}

void SDLTexture::RestoreWindowPosition()
{
    int x, y, width, height, maximised;
    if (sscanf(GetOption(windowpos).c_str(), "%d,%d,%d,%d,%d", &x, &y, &width, &height, &maximised) == 5)
    {
        SDL_SetWindowPosition(m_window, x, y);
        SDL_SetWindowSize(m_window, width, height);

        if (maximised)
            SDL_MaximizeWindow(m_window);
    }
}

#endif // HAVE_LIBSDL2 || HAVE_LIBSDL3
