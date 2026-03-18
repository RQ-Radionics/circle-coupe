// Part of SimCoupe - A SAM Coupe emulator
//
// OSD.cpp: Circle bare-metal "OS-dependent" functions
//
// No SDL, no desktop OS, no clipboard.
// Files live on the SD card mounted at /simcoupe.

#include "SimCoupe.h"
#include "OSD.h"
#include "Options.h"
#include "Sound.h"

bool OSD::Init()
{
    // Use audio sync so Sound.cpp timing is driven by PWM/HDMI DMA backpressure,
    // not by high_resolution_clock sleep (which is unreliable on this toolchain).
    SetOption(audiosync, true);

    // 8px border — gives 544x208 (pGuiScreen 544x416) which is wide enough
    // for the file browser dialog (527px wide). visiblearea=0 (512px) clips it.
    SetOption(visiblearea, 1);

    return true;
}

void OSD::Exit()
{
}

std::string OSD::MakeFilePath(PathType type, const std::string& filename)
{
    std::string base;

    switch (type)
    {
    case PathType::Settings:  base = "/simcoupe";        break;
    case PathType::Input:
        base = GetOption(inpath).empty() ? "/simcoupe/disks"  : GetOption(inpath);
        break;
    case PathType::Output:
        base = GetOption(outpath).empty() ? "/simcoupe/output" : GetOption(outpath);
        break;
    case PathType::Resource:  base = "/simcoupe";        break;
    default:                  base = "/simcoupe";        break;
    }

    if (!fs::exists(base))
    {
        std::error_code ec;
        fs::create_directories(base, ec);
    }

    if (filename.empty())
        return base;

    return (fs::path(base) / filename).string();
}

bool OSD::IsHidden(const std::string& path)
{
    auto p = fs::path(path);
    return p.has_filename() && p.filename().string().front() == '.';
}

std::string OSD::GetClipboardText()
{
    return {};
}

void OSD::SetClipboardText(const std::string& /*str*/)
{
}

void OSD::DebugTrace(const std::string& /*str*/)
{
}
