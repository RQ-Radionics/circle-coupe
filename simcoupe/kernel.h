// kernel.h — SimCoupe Circle kernel (monocore + VCHIQ audio test)
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
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <vc4/vchiq/vchiqdevice.h>

enum TShutdownMode { ShutdownNone, ShutdownHalt, ShutdownReboot };

class CKernel
{
public:
    CKernel();
    ~CKernel();

    boolean Initialize();
    TShutdownMode Run();

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
    CVCHIQDevice        m_VCHIQ;
};
