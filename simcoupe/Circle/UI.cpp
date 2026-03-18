// Part of SimCoupe - A SAM Coupe emulator
//
// UI.cpp: Circle bare-metal user interface
//
// No SDL, no window manager, no event loop.
// CheckEvents() polls keyboard via Input::Update() and returns true.
// CreateVideo() returns our Circle IVideoBase implementation.

#include "SimCoupe.h"
#include "UI.h"

#include "Actions.h"
#include "CPU.h"
#include "Frame.h"
#include "GUI.h"
#include "Input.h"
#include "Options.h"
#include "Sound.h"
#include "Video.h"

bool g_fActive = true;

bool UI::Init(bool /*fFirstInit_*/)
{
    return true;
}

void UI::Exit(bool /*fReInit_*/)
{
}

bool UI::CheckEvents()
{
    if (GUI::IsActive())
        Input::Update();

    // No quit event on bare-metal — run forever
    return true;
}

void UI::ShowMessage(MsgType type, const std::string& str)
{
    if (type == MsgType::Info)
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbInformation));
    else if (type == MsgType::Warning)
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbWarning));
    else
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbError));
}

bool UI::DoAction(Action action, bool pressed)
{
    if (pressed)
    {
        switch (action)
        {
        case Action::ExitApp:
            // No exit on bare-metal — just halt
            CPU::Exit();
            return true;
        default:
            return false;
        }
    }
    return false;
}

std::unique_ptr<IVideoBase> UI::CreateVideo()
{
    // Defined in Video.cpp
    extern std::unique_ptr<IVideoBase> CreateCircleVideo();
    return CreateCircleVideo();
}
