// Circle/Stubs.cpp — stub implementations for unsupported hardware on RPi3
//
// MIDI: no MIDI hardware on RPi3 bare-metal
// DeviceHardDisk: no raw IDE on RPi3 (could use USB mass storage later)
// circle_delay_ns / circle_get_clock_ticks64: defined in SDL_circlefb.cpp
//   but also needed here for Sound.cpp linking

#include "SimCoupe.h"
#include "MIDI.h"

// ---- MIDI stubs ----

MidiDevice::MidiDevice() {}
MidiDevice::~MidiDevice() {}

bool MidiDevice::SetDevice(const std::string& /*device*/) { return false; }
uint8_t MidiDevice::In(uint16_t /*port*/) { return 0xff; }
void MidiDevice::Out(uint16_t /*port*/, uint8_t /*val*/) {}

// ---- DeviceHardDisk stub ----
#include "HardDisk.h"
#include "IDEDisk.h"

bool DeviceHardDisk::Open(bool /*read_only*/) { return false; }
bool DeviceHardDisk::ReadSector(unsigned int /*sector*/, uint8_t* /*buf*/) { return false; }
bool DeviceHardDisk::WriteSector(unsigned int /*sector*/, uint8_t* /*buf*/) { return false; }

// ---- circle_delay_ns / circle_get_clock_ticks64 ----

#include <circle/timer.h>

extern "C" unsigned long long circle_get_clock_ticks64(void)
{
#if AARCH == 32
    unsigned long nLow, nHigh;
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(nLow), "=r"(nHigh));
    unsigned long long cntpct = ((unsigned long long)nHigh << 32) | nLow;
    unsigned long cntfrq;
    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(cntfrq));
    return cntpct * 1000000ULL / (unsigned long long)cntfrq;
#else
    return (unsigned long long)CTimer::GetClockTicks64();
#endif
}

extern "C" void circle_delay_ns(unsigned long long ns)
{
    if (ns > 0 && ns < 2000000000ULL)
        CTimer::Get()->usDelay((unsigned)(ns / 1000 + 1));
}
