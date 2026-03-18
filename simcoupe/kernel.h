//
// kernel.h - SimCoupe Circle kernel for Raspberry Pi 3B (AArch32)
//
// Multi-core architecture (like bmc64):
//   Core 0: Initialize() + Run(0) -- USB host, IRQs, IO loop (UpdatePlugAndPlay)
//   Core 1: Run(1) -- SimCoupeMain() -- Z80 emulation, video, audio
//
#pragma once

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sched/scheduler.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/multicore.h>
#include <circle/types.h>

// SD card + FatFs
#include <SDCard/emmc.h>

enum TShutdownMode { ShutdownNone, ShutdownHalt, ShutdownReboot };

class CKernel : public CMultiCoreSupport
{
public:
    CKernel();
    ~CKernel();

    boolean Initialize();

    // Core 0: IO loop (USB plug-and-play, scheduler)
    TShutdownMode Run();

    // CMultiCoreSupport: called on each secondary core
    void Run(unsigned nCore) override;

private:
    CMemorySystem       m_Memory;
    CActLED             m_ActLED;
    CKernelOptions      m_Options;
    CDeviceNameService  m_DeviceNameService;
    CSerialDevice       m_Serial;
    CExceptionHandler   m_ExceptionHandler;
    CInterruptSystem    m_Interrupt;
    CTimer              m_Timer;
    CLogger             m_Logger;
    CScheduler          m_Scheduler;
    CUSBHCIDevice       m_USBHCI;
    CEMMCDevice         m_EMMC;

    // Synchronization: core 1 spins on this until core 0 sets it
    // Simple volatile - no spinlock needed, only written once by core 0
    volatile bool       m_bLaunch;
};
