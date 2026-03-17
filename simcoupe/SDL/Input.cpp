// Part of SimCoupe - A SAM Coupe emulator
//
// Input.cpp: SDL keyboard, mouse and joystick input
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

#ifdef __circle__
extern "C" void *circle_fb_get_buffer(void);
extern "C" unsigned circle_fb_get_pitch(void);
extern "C" unsigned circle_fb_get_width(void);
extern "C" unsigned circle_fb_get_height(void);

// Pending F-key action (set from USB callback, processed from main loop)
static volatile int  s_pending_fn     = 0;
static volatile bool s_pending_ctrl   = false;
static volatile bool s_pending_alt    = false;
static volatile bool s_pending_shift  = false;

static void dbg_paint_scancode(unsigned scancode)
{
    // Paint a white bar whose width = scancode value at the bottom of screen
    void *fb = circle_fb_get_buffer();
    if (!fb) return;
    unsigned pitch = circle_fb_get_pitch();
    unsigned fb_w   = circle_fb_get_width();
    unsigned fb_h   = circle_fb_get_height();

    // Clear bottom 20 rows to black
    for (unsigned y = fb_h - 20; y < fb_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        for (unsigned x = 0; x < fb_w; x++) row[x] = 0;
    }
    // Paint white bar of width = scancode (capped at fb_w)
    unsigned bar_w = scancode < fb_w ? scancode : fb_w;
    for (unsigned y = fb_h - 20; y < fb_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        for (unsigned x = 0; x < bar_w; x++) row[x] = 0x00FFFFFF;
    }
}
#endif

#include "Actions.h"
#include "Frame.h"
#include "GUI.h"
#include "Input.h"
#include "Keyin.h"
#include "SAMIO.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Options.h"
#include "Mouse.h"
#include "UI.h"

//#define USE_JOYPOLLING

#ifdef HAVE_LIBSDL3
SDL_JoystickID nJoystick1 = 0, nJoystick2 = 0;
#else
int nJoystick1 = -1, nJoystick2 = -1;
#endif
SDL_Joystick* pJoystick1, * pJoystick2;

bool fMouseActive, fKeyboardActive;
int nLastKey, nLastMods;
#ifdef HAVE_LIBSDL3
const bool* pKeyStates;
#else
const Uint8* pKeyStates;
#endif

////////////////////////////////////////////////////////////////////////////////

bool Input::Init()
{
    Exit();

#ifdef HAVE_LIBSDL3
    // Enumerate joysticks via SDL3 API
    int nJoysticks = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&nJoysticks);
    if (joysticks)
    {
        for (int i = 0; i < nJoysticks; i++)
        {
            SDL_JoystickID id = joysticks[i];
            const char* name = SDL_GetJoystickNameForID(id);
            if (!name) continue;

            // Ignore VirtualBox devices
            if (!strncmp(name, "VirtualBox", 10))
                continue;

            if (!pJoystick1 && (name == GetOption(joydev1) || GetOption(joydev1).empty()))
                pJoystick1 = SDL_OpenJoystick(nJoystick1 = id);
            else if (!pJoystick2 && (name == GetOption(joydev2) || GetOption(joydev2).empty()))
                pJoystick2 = SDL_OpenJoystick(nJoystick2 = id);
        }
        SDL_free(joysticks);
    }
#else
    // Loop through the available devices for the ones to use (if any)
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        // Ignore VirtualBox devices, as the default USB Tablet option
        // is seen as a joystick, which generates unwanted inputs
        if (!strncmp(SDL_JoystickNameForIndex(i), "VirtualBox", 10))
            continue;

        // Match against the required joystick names, or auto-select the first available
        if (!pJoystick1 && ((SDL_JoystickNameForIndex(i) == GetOption(joydev1)) || GetOption(joydev1).empty()))
            pJoystick1 = SDL_JoystickOpen(nJoystick1 = i);
        else if (!pJoystick2 && ((SDL_JoystickNameForIndex(i) == GetOption(joydev2)) || GetOption(joydev2).empty()))
            pJoystick2 = SDL_JoystickOpen(nJoystick2 = i);
    }
#endif

#ifdef USE_JOYPOLLING
    // Disable joystick events as we'll poll ourselves when necessary
#ifdef HAVE_LIBSDL3
    SDL_SetJoystickEventsEnabled(false);
#else
    SDL_JoystickEventState(SDL_DISABLE);
#endif
#endif

    Keyboard::Init();

#ifdef HAVE_LIBSDL3
    SDL_StartTextInput(SDL_GetKeyboardFocus());
    SDL_SetTextInputArea(SDL_GetKeyboardFocus(), nullptr, 0);
#else
    SDL_StartTextInput();
    SDL_SetTextInputRect(nullptr);
#endif

    pKeyStates = SDL_GetKeyboardState(nullptr);

    fMouseActive = false;

    return true;
}

void Input::Exit()
{
    if (pJoystick1) { SDL_CloseJoystick(pJoystick1); pJoystick1 = nullptr; nJoystick1 = 0; }
    if (pJoystick2) { SDL_CloseJoystick(pJoystick2); pJoystick2 = nullptr; nJoystick2 = 0; }
}

// Return whether the emulation is using the mouse
bool Input::IsMouseAcquired()
{
    return fMouseActive;
}

void Input::AcquireMouse(bool active)
{
    if (fMouseActive == active)
        return;

    if (GetOption(mouse))
    {
#ifdef HAVE_LIBSDL3
        if (active) SDL_HideCursor(); else SDL_ShowCursor();
        Video::MouseRelative();
        SDL_CaptureMouse(active);
#else
        SDL_ShowCursor(active ? SDL_DISABLE : SDL_ENABLE);
        Video::MouseRelative();
        SDL_CaptureMouse(active ? SDL_TRUE : SDL_FALSE);
#endif
    }

    fMouseActive = active;
}


// Purge pending keyboard and/or mouse events
void Input::Purge()
{
    // Remove queued input messages
    SDL_Event event;
#ifdef HAVE_LIBSDL3
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP));
#else
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_JOYBUTTONUP));
#endif

    // Discard relative motion and clear the key modifiers
#ifdef HAVE_LIBSDL3
    float fx, fy;
    SDL_GetRelativeMouseState(&fx, &fy);
    SDL_SetModState(SDL_KMOD_NONE);
#else
    int n;
    SDL_GetRelativeMouseState(&n, &n);
    SDL_SetModState(KMOD_NONE);
#endif

    Keyboard::Purge();
}


#ifdef USE_JOYPOLLING
// Read the specified joystick
static void ReadJoystick(int nJoystick_, SDL_Joystick* pJoystick_, int nTolerance_)
{
    int nPosition = HJ_CENTRE;
    uint32_t dwButtons = 0;
    Uint8 bHat = 0;
    int i;

    int nDeadZone = 32768 * nTolerance_ / 100;
    int nButtons = SDL_GetNumJoystickButtons(pJoystick_);
    int nHats = SDL_GetNumJoystickHats(pJoystick_);
    int nX = SDL_GetJoystickAxis(pJoystick_, 0);
    int nY = SDL_GetJoystickAxis(pJoystick_, 1);

    for (i = 0; i < nButtons; i++)
        dwButtons |= SDL_GetJoystickButton(pJoystick_, i) << i;

    for (i = 0; i < nHats; i++)
        bHat |= SDL_GetJoystickHat(pJoystick_, i);


    if ((nX < -nDeadZone) || (bHat & SDL_HAT_LEFT))  nPosition |= HJ_LEFT;
    if ((nX > nDeadZone) || (bHat & SDL_HAT_RIGHT)) nPosition |= HJ_RIGHT;
    if ((nY < -nDeadZone) || (bHat & SDL_HAT_UP))    nPosition |= HJ_UP;
    if ((nY > nDeadZone) || (bHat & SDL_HAT_DOWN))  nPosition |= HJ_DOWN;

    Joystick::SetPosition(nJoystick_, nPosition);
    Joystick::SetButtons(nJoystick_, dwButtons);
}
#endif // USE_JOYPOLLING


// Process SDL event messages
bool Input::FilterEvent(SDL_Event* pEvent_)
{
    switch (pEvent_->type)
    {
#ifdef HAVE_LIBSDL3
    case SDL_EVENT_TEXT_INPUT:
#else
    case SDL_TEXTINPUT:
#endif
    {
        SDL_TextInputEvent* pEvent = &pEvent_->text;
#ifdef HAVE_LIBSDL3
        int nChr = pEvent->text[0];
        auto pbText = reinterpret_cast<const uint8_t*>(pEvent->text);
#else
        int nChr = pEvent->text[0];
        auto pbText = reinterpret_cast<uint8_t*>(pEvent->text);
#endif

        // Ignore symbols from the keypad
        if ((nLastKey >= HK_KP0 && nLastKey <= HK_KP9) || (nLastKey >= HK_KPPLUS && nLastKey <= HK_KPDECIMAL))
            break;
        else if (nLastKey == HK_SECTION)
            break;
        else if (pbText[1])
        {
            if (pbText[0] == 0xc2 && pbText[1] == 0xa3)
                nChr = 163; // GBP
            else if (pbText[0] == 0xc2 && pbText[1] == 0xa7)
                nChr = 167; // section symbol
            else if (pbText[0] == 0xc2 && pbText[1] == 0xb1)
                nChr = 177; // +/-
            else
                break;
        }

        TRACE("SDL_TEXTINPUT: {} (nLastKey={}, nLastMods={})\n", pEvent->text, nLastKey, nLastMods);
        Keyboard::SetKey(nLastKey, true, nLastMods, nChr);
        GUI::SendMessage(GM_CHAR, nChr, nLastMods);
        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
#else
    case SDL_KEYDOWN:
    case SDL_KEYUP:
#endif
    {
        SDL_KeyboardEvent* pEvent = &pEvent_->key;

#ifdef HAVE_LIBSDL3


        bool fPress = pEvent->down;
        if (fPress)
            SDL_HideCursor();

#ifdef __circle__
        if (fPress)
            dbg_paint_scancode((unsigned)pEvent->scancode);
#endif

        // Ignore key repeats unless the GUI is active
        if (pEvent->repeat && !GUI::IsActive())
            break;

        int nKey = MapKey(pEvent->key);
        int nMods = ((pEvent->mod & SDL_KMOD_SHIFT) ? HM_SHIFT : 0) |
            ((pEvent->mod & SDL_KMOD_LCTRL) ? HM_CTRL : 0) |
            ((pEvent->mod & SDL_KMOD_LALT) ? HM_ALT : 0);
#else
        SDL_Keysym* pKey = &pEvent->keysym;

        bool fPress = pEvent->type == SDL_KEYDOWN;
        if (fPress)
            SDL_ShowCursor(SDL_DISABLE);

        // Ignore key repeats unless the GUI is active
        if (pEvent->repeat && !GUI::IsActive())
            break;

        int nKey = MapKey(pKey->sym);
        int nMods = ((pKey->mod & KMOD_SHIFT) ? HM_SHIFT : 0) |
            ((pKey->mod & KMOD_LCTRL) ? HM_CTRL : 0) |
            ((pKey->mod & KMOD_LALT) ? HM_ALT : 0);
#endif

        int nChr = 0;
        if ((nKey < HK_SPACE) ||
#ifdef HAVE_LIBSDL3
            (nKey < HK_MIN && (pEvent->mod & SDL_KMOD_CTRL)) ||
#else
            (nKey < HK_MIN && (pKey->mod & KMOD_CTRL)) ||
#endif
            (nKey >= HK_LEFT && nKey != HK_NONE))
        {
            nChr = nKey;
        }

#ifdef HAVE_LIBSDL3
        TRACE("SDL_KEY{} ({:x} -> {:x})\n", fPress ? "DOWN" : "UP", pEvent->key, nKey);
#else
        TRACE("SDL_KEY{} ({:x} -> {:x})\n", fPress ? "DOWN" : "UP", pKey->sym, nKey);
#endif

        if (fPress)
        {
            nLastKey = nKey;
            nLastMods = nMods;
        }

#ifdef HAVE_LIBSDL3
        bool fCtrl = !!(pEvent->mod & SDL_KMOD_CTRL);
        bool fAlt = !!(pEvent->mod & SDL_KMOD_ALT);
        bool fShift = !!(pEvent->mod & SDL_KMOD_SHIFT);
#else
        bool fCtrl = !!(pKey->mod & KMOD_CTRL);
        bool fAlt = !!(pKey->mod & KMOD_ALT);
        bool fShift = !!(pKey->mod & KMOD_SHIFT);
#endif

        // Unpause on key press if paused, so the user doesn't think we've hung
        if (fPress && g_fPaused && nKey != HK_PAUSE)
            Actions::Do(Action::Pause);

        // Use key repeats for GUI mode only
        if (fKeyboardActive == GUI::IsActive())
        {
            fKeyboardActive = !fKeyboardActive;
        }

        // Check for the Windows key, for use as a modifier
        bool fWin = pKeyStates[SDL_SCANCODE_LGUI] || pKeyStates[SDL_SCANCODE_RGUI];
        if (!fWin)
        {
            if (nKey >= HK_F1 && nKey <= HK_F12)
            {
                Actions::Key(nKey - HK_F1 + 1, fPress, fCtrl, fAlt, fShift);
                break;
            }
            else if (nKey >= '0' && nKey <= '9' && fAlt && !fCtrl && !fShift)
            {
                auto scale_2x = nKey - '0' + 1;
                auto height = Frame::Height() * scale_2x / 2;
                Video::ResizeWindow(height);
            }
        }

        if (GUI::IsActive())
        {
            // Pass any printable characters to the GUI
            if (fPress && nChr)
                GUI::SendMessage(GM_CHAR, nChr, nMods);

            break;
        }

        // Some additional function keys
        bool fAction = true;
        switch (nKey)
        {
        case HK_RETURN:     fAction = fAlt; if (fAction) Actions::Do(Action::ToggleFullscreen, fPress); break;
        case HK_KPDIVIDE:   Actions::Do(Action::Debugger, fPress); break;
        case HK_KPMULT:     Actions::Do(fCtrl ? Action::Reset : Action::SpeedTurbo, fPress); break;
        case HK_KPPLUS:     Actions::Do(fCtrl ? Action::SpeedTurbo : Action::SpeedFaster, fPress); break;
        case HK_KPMINUS:    Actions::Do(fCtrl ? Action::SpeedNormal : Action::SpeedSlower, fPress); break;

        case HK_PRINT:      Actions::Do(Action::SavePNG, fPress); break;
        case HK_SCROLL:
        case HK_PAUSE:      Actions::Do(fCtrl ? Action::Reset : fShift ? Action::FrameStep : Action::Pause, fPress); break;

        default:            fAction = false; break;
        }

        // Have we processed the key?
        if (fAction)
            break;

        // Optionally release the mouse capture if Esc is pressed
        if (fPress && nKey == HK_ESC && GetOption(mouseesc))
        {
            Actions::Do(Action::ReleaseMouse);
            Keyin::Stop();
        }

        Keyboard::SetKey(nKey, fPress, nMods, nChr);
        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_MOUSE_MOTION:
#else
    case SDL_MOUSEMOTION:
#endif
    {
        int x = static_cast<int>(pEvent_->motion.x);
        int y = static_cast<int>(pEvent_->motion.y);

#ifdef HAVE_LIBSDL3
        bool hide_cursor = fMouseActive && !GUI::IsActive();
        if (hide_cursor) SDL_HideCursor(); else SDL_ShowCursor();
#else
        bool hide_cursor = fMouseActive && !GUI::IsActive();
        SDL_ShowCursor(hide_cursor ? SDL_DISABLE : SDL_ENABLE);
#endif

        // Mouse in use by the GUI?
        if (GUI::IsActive())
        {
            Video::NativeToSam(x, y);
            GUI::SendMessage(GM_MOUSEMOVE, x, y);
        }
        else if (fMouseActive)
        {
            auto [dx, dy] = Video::MouseRelative();
            if (dx || dy)
            {
                TRACE("Mouse: {} {}\n", dx, -dy);
                pMouse->Move(dx, -dy);
            }
        }
        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
#else
    case SDL_MOUSEBUTTONDOWN:
#endif
    {
        static std::optional<std::chrono::steady_clock::time_point> last_click_time;
        static int nLastButton = -1;

        int x = static_cast<int>(pEvent_->button.x);
        int y = static_cast<int>(pEvent_->button.y);
        auto now = std::chrono::steady_clock::now();
        bool double_click = (pEvent_->button.button == nLastButton) &&
            last_click_time && ((now - *last_click_time) < DOUBLE_CLICK_TIME);

        // Button presses go to the GUI if it's active
        if (GUI::IsActive())
        {
            switch (pEvent_->button.button)
            {
            case 4:
                GUI::SendMessage(GM_MOUSEWHEEL, -1);
                break;
            case 5:
                GUI::SendMessage(GM_MOUSEWHEEL, 1);
                break;
            default:
                Video::NativeToSam(x, y);
                GUI::SendMessage(GM_BUTTONDOWN, x, y);
                break;
            }
        }

        // Pass the button click through if the mouse is active
        else if (fMouseActive)
        {
            pMouse->SetButton(pEvent_->button.button, true);
            TRACE("Mouse button {} pressed\n", pEvent_->button.button);
        }

        // If the mouse interface is enabled and being read by something other than the ROM, a left-click acquires it
        // Otherwise a double-click is required to forcibly acquire it
        else if (GetOption(mouse) && pEvent_->button.button == 1 && (pMouse->IsActive() || double_click))
            AcquireMouse();

        // Remember the last click click time and button, for double-click tracking
        nLastButton = pEvent_->button.button;
        last_click_time = std::chrono::steady_clock::now();

        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_MOUSE_BUTTON_UP:
#else
    case SDL_MOUSEBUTTONUP:
#endif
        // Button presses go to the GUI if it's active
        if (GUI::IsActive())
        {
            int x = static_cast<int>(pEvent_->button.x);
            int y = static_cast<int>(pEvent_->button.y);
            Video::NativeToSam(x, y);
            GUI::SendMessage(GM_BUTTONUP, x, y);
        }
        else if (fMouseActive)
        {
            TRACE("Mouse button {} released\n", pEvent_->button.button);
            pMouse->SetButton(pEvent_->button.button, false);
        }
        break;

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_MOUSE_WHEEL:
#else
    case SDL_MOUSEWHEEL:
#endif
        if (GUI::IsActive())
        {
            if (pEvent_->wheel.y > 0)
                GUI::SendMessage(GM_MOUSEWHEEL, -1);
            else if (pEvent_->wheel.y < 0)
                GUI::SendMessage(GM_MOUSEWHEEL, 1);
        }
        break;

#ifndef USE_JOYPOLLING

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
#else
    case SDL_JOYAXISMOTION:
#endif
    {
        SDL_JoyAxisEvent* p = &pEvent_->jaxis;
        int nJoystick = (p->which == nJoystick1) ? 0 : 1;
        int nDeadZone = 32768 * (!nJoystick ? GetOption(deadzone1) : GetOption(deadzone2)) / 100;

        if (p->axis == 0)
            Joystick::SetX(nJoystick, (p->value < -nDeadZone) ? HJ_LEFT : (p->value > nDeadZone) ? HJ_RIGHT : HJ_CENTRE);
        else if (p->axis == 1)
            Joystick::SetY(nJoystick, (p->value < -nDeadZone) ? HJ_UP : (p->value > nDeadZone) ? HJ_DOWN : HJ_CENTRE);

        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
#else
    case SDL_JOYHATMOTION:
#endif
    {
        SDL_JoyHatEvent* p = &pEvent_->jhat;
        int nJoystick = (p->which == nJoystick1) ? 0 : 1;
        Uint8 bHat = p->value;

        int nPosition = HJ_CENTRE;
        if (bHat & SDL_HAT_LEFT)  nPosition |= HJ_LEFT;
        if (bHat & SDL_HAT_RIGHT) nPosition |= HJ_RIGHT;
        if (bHat & SDL_HAT_DOWN)  nPosition |= HJ_DOWN;
        if (bHat & SDL_HAT_UP)    nPosition |= HJ_UP;

        Joystick::SetPosition(nJoystick, nPosition);
        break;
    }

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
#else
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
#endif
    {
        SDL_JoyButtonEvent* p = &pEvent_->jbutton;
        int nJoystick = (p->which == nJoystick1) ? 0 : 1;

#ifdef HAVE_LIBSDL3
        Joystick::SetButton(nJoystick, p->button, p->down);
#else
        Joystick::SetButton(nJoystick, p->button, (p->state == SDL_PRESSED));
#endif
        break;
    }
#endif

#ifdef HAVE_LIBSDL3
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    {
        AcquireMouse(false);
        break;
    }
#else
    case SDL_WINDOWEVENT:
    {
        if (pEvent_->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        {
            AcquireMouse(false);
        }
        break;
    }
#endif
    }

    // Allow additional event processing
    return false;
}


void Input::Update()
{
#ifdef __circle__
    // Process any pending F-key action from the USB callback
    if (s_pending_fn != 0) {
        int fn = s_pending_fn;
        bool ctrl = s_pending_ctrl, alt = s_pending_alt, shift = s_pending_shift;
        s_pending_fn = 0;
        Actions::Key(fn, true, ctrl, alt, shift);
    }
#endif

#ifdef USE_JOYPOLLING
    // Either joystick active?
    if (pJoystick1 || pJoystick2)
    {
        // Update and read the current joystick states
        SDL_UpdateJoysticks();
        if (pJoystick1) ReadJoystick(0, pJoystick1, GetOption(deadzone1));
        if (pJoystick2) ReadJoystick(1, pJoystick2, GetOption(deadzone2));
    }
#endif

    Keyboard::Update();

    // CapsLock/NumLock are toggle keys in SDL and must be released manually
    Keyboard::SetKey(HK_CAPSLOCK, false);
    Keyboard::SetKey(HK_NUMLOCK, false);
}


int Input::MapChar(int nChar_, int* /*pnMods_*/)
{
#ifdef __circle__
    // On bare-metal, circle_simcoupe_key calls Keyboard::SetKey with the ASCII
    // character value directly (e.g. 'a'=97). PrepareKeyTable calls MapChar to
    // find which key_states slot to check. Return the ASCII value as-is so the
    // slot matches what SetKey wrote.
    if (nChar_ > 0 && nChar_ < HK_MIN)
        return nChar_;
#else
    // Regular characters details aren't known until the key press
    if (nChar_ < HK_MIN)
        return 0;
#endif

    if (nChar_ >= HK_MIN && nChar_ < HK_MAX)
        return nChar_;

    return 0;
}

int Input::MapKey(int nKey_)
{
    // Host keycode
    switch (nKey_)
    {
    case SDLK_LSHIFT:   return HK_LSHIFT;
    case SDLK_RSHIFT:   return HK_RSHIFT;
    case SDLK_LCTRL:    return HK_LCTRL;
    case SDLK_RCTRL:    return HK_RCTRL;
    case SDLK_LALT:     return HK_LALT;
    case SDLK_RALT:     return HK_RALT;
    case SDLK_LGUI:     return HK_LWIN;
    case SDLK_RGUI:     return HK_RWIN;

    case SDLK_LEFT:     return HK_LEFT;
    case SDLK_RIGHT:    return HK_RIGHT;
    case SDLK_UP:       return HK_UP;
    case SDLK_DOWN:     return HK_DOWN;

    case SDLK_KP_0:     return HK_KP0;
    case SDLK_KP_1:     return HK_KP1;
    case SDLK_KP_2:     return HK_KP2;
    case SDLK_KP_3:     return HK_KP3;
    case SDLK_KP_4:     return HK_KP4;
    case SDLK_KP_5:     return HK_KP5;
    case SDLK_KP_6:     return HK_KP6;
    case SDLK_KP_7:     return HK_KP7;
    case SDLK_KP_8:     return HK_KP8;
    case SDLK_KP_9:     return HK_KP9;

    case SDLK_F1:       return HK_F1;
    case SDLK_F2:       return HK_F2;
    case SDLK_F3:       return HK_F3;
    case SDLK_F4:       return HK_F4;
    case SDLK_F5:       return HK_F5;
    case SDLK_F6:       return HK_F6;
    case SDLK_F7:       return HK_F7;
    case SDLK_F8:       return HK_F8;
    case SDLK_F9:       return HK_F9;
    case SDLK_F10:      return HK_F10;
    case SDLK_F11:      return HK_F11;
    case SDLK_F12:      return HK_F12;

    case SDLK_CAPSLOCK: return HK_CAPSLOCK;
    case SDLK_NUMLOCKCLEAR: return HK_NUMLOCK;
    case SDLK_KP_PLUS:  return HK_KPPLUS;
    case SDLK_KP_MINUS: return HK_KPMINUS;
    case SDLK_KP_MULTIPLY: return HK_KPMULT;
    case SDLK_KP_DIVIDE:return HK_KPDIVIDE;
    case SDLK_KP_ENTER: return HK_KPENTER;
    case SDLK_KP_PERIOD:return HK_KPDECIMAL;

    case SDLK_INSERT:   return HK_INSERT;
    case SDLK_DELETE:   return HK_DELETE;
    case SDLK_HOME:     return HK_HOME;
    case SDLK_END:      return HK_END;
    case SDLK_PAGEUP:   return HK_PGUP;
    case SDLK_PAGEDOWN: return HK_PGDN;

    case SDLK_ESCAPE:   return HK_ESC;
    case SDLK_TAB:      return HK_TAB;
    case SDLK_BACKSPACE:return HK_BACKSPACE;
    case SDLK_RETURN:   return HK_RETURN;

    case SDLK_PRINTSCREEN: return HK_PRINT;
    case SDLK_SCROLLLOCK:return HK_SCROLL;
    case SDLK_PAUSE:    return HK_PAUSE;

    case SDLK_APPLICATION: return HK_APPS;
    }

    return (nKey_ && nKey_ < HK_MIN) ? nKey_ : HK_NONE;
}

#ifdef __circle__
// USB HID scancode -> HK_ constant (same values as SDL scancodes)
// Called directly from kernel.cpp USB handler, bypassing SDL event queue.
static int HKFromUsbHid(unsigned hid)
{
    // USB HID scancodes match SDL scancodes which MapKey() converts via SDLK.
    // We map USB HID directly to HK_ here.
    switch (hid) {
    // Letters a-z: HID 4-29
    case 4: return 'a'; case 5: return 'b'; case 6: return 'c';
    case 7: return 'd'; case 8: return 'e'; case 9: return 'f';
    case 10: return 'g'; case 11: return 'h'; case 12: return 'i';
    case 13: return 'j'; case 14: return 'k'; case 15: return 'l';
    case 16: return 'm'; case 17: return 'n'; case 18: return 'o';
    case 19: return 'p'; case 20: return 'q'; case 21: return 'r';
    case 22: return 's'; case 23: return 't'; case 24: return 'u';
    case 25: return 'v'; case 26: return 'w'; case 27: return 'x';
    case 28: return 'y'; case 29: return 'z';
    // Numbers 1-0: HID 30-39
    case 30: return '1'; case 31: return '2'; case 32: return '3';
    case 33: return '4'; case 34: return '5'; case 35: return '6';
    case 36: return '7'; case 37: return '8'; case 38: return '9';
    case 39: return '0';
    // Special keys
    case 40: return HK_RETURN;
    case 41: return HK_ESC;
    case 42: return HK_BACKSPACE;
    case 43: return HK_TAB;
    case 44: return HK_SPACE;
    case 45: return '-';
    case 46: return '=';
    case 47: return '[';
    case 48: return ']';
    case 49: return '\\';
    case 51: return ';';
    case 52: return '\'';
    case 53: return '`';
    case 54: return ',';
    case 55: return '.';
    case 56: return '/';
    case 57: return HK_CAPSLOCK;
    // F keys: HID 58-69
    case 58: return HK_F1;  case 59: return HK_F2;  case 60: return HK_F3;
    case 61: return HK_F4;  case 62: return HK_F5;  case 63: return HK_F6;
    case 64: return HK_F7;  case 65: return HK_F8;  case 66: return HK_F9;
    case 67: return HK_F10; case 68: return HK_F11; case 69: return HK_F12;
    // Navigation
    case 73: return HK_INSERT;  case 74: return HK_HOME;
    case 75: return HK_PGUP;    case 76: return HK_DELETE;
    case 77: return HK_END;     case 78: return HK_PGDN;
    case 79: return HK_RIGHT;   case 80: return HK_LEFT;
    case 81: return HK_DOWN;    case 82: return HK_UP;
    // Modifiers: HID 224-231
    case 224: return HK_LCTRL;  case 225: return HK_LSHIFT;
    case 226: return HK_LALT;   case 228: return HK_RCTRL;
    case 229: return HK_RSHIFT; case 230: return HK_RALT;
    }
    return HK_NONE;
}

extern "C" void circle_simcoupe_key(unsigned hid_scancode, int pressed,
                                     int mod_shift, int mod_ctrl, int mod_alt)
{

    int nKey = HKFromUsbHid(hid_scancode);
    if (nKey == HK_NONE) return;

    bool fPress = (pressed != 0);
    int nMods = (mod_shift ? HM_SHIFT : 0) |
                (mod_ctrl  ? HM_CTRL  : 0) |
                (mod_alt   ? HM_ALT   : 0);

    if (GUI::IsActive()) {
        // GUI is active: send key events directly to the GUI
        if (fPress) {
            GUI::SendMessage(GM_CHAR, nKey, nMods);
        }
        return;
    }

    // For F keys, store action to be processed from main loop
    // (USB callback context may conflict with FatFs when GUI opens disk browser)
    if (nKey >= HK_F1 && nKey <= HK_F12) {
        if (fPress) {
            s_pending_fn    = nKey - HK_F1 + 1;
            s_pending_ctrl  = (mod_ctrl != 0);
            s_pending_alt   = (mod_alt != 0);
            s_pending_shift = (mod_shift != 0);
        }
        Keyboard::SetKey(nKey, fPress, nMods);
    } else {
        Keyboard::SetKey(nKey, fPress, nMods);
    }
}
#endif // __circle__
