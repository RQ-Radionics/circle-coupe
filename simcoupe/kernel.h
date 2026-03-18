// kernel.h — SimCoupe Circle kernel (no SDL, multicore)
//
// Core 0: Initialize() + Run() — USB, IRQs, scheduler
// Core 1: Run(1) — SimCoupe Z80 + video + audio

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
#include <SDCard/emmc.h>

enum TShutdownMode { ShutdownNone, ShutdownHalt, ShutdownReboot };

class CKernel : public CMultiCoreSupport
{
public:
    CKernel();
    ~CKernel();

    boolean Initialize();
    TShutdownMode Run();           // Core 0
    void Run(unsigned nCore) override;  // Core 1+

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

    volatile bool       m_bLaunch;
};
