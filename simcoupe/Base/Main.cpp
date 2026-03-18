// Part of SimCoupe - A SAM Coupe emulator
//
// Main.cpp: Main entry point

#include "SimCoupe.h"
#include "Main.h"

#include "CPU.h"
#include "Frame.h"
#include "GUI.h"
#include "Input.h"
#include "Options.h"
#include "Sound.h"
#include "UI.h"
#include "Video.h"


extern "C" int main(int argc_, char* argv_[])
{
    if (Main::Init(argc_, argv_))
        CPU::Run();

    Main::Exit();

    return 0;
}

namespace Main
{

bool Init(int argc_, char* argv_[])
{
    if (libspectrum_init() != LIBSPECTRUM_ERROR_NONE)
        return false;

    if (!Options::Load(argc_, argv_))
        return false;

    return OSD::Init() && Frame::Init() && CPU::Init(true) && UI::Init() &&
           Sound::Init() && Input::Init() && Video::Init();
}

void Exit()
{
    GUI::Stop();

    Video::Exit();
    Input::Exit();
    Sound::Exit();
    UI::Exit();
    CPU::Exit();
    Frame::Exit();
    OSD::Exit();

    Options::Save();
    libspectrum_end();
}

} // namespace Main
