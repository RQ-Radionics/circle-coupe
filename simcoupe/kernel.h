// kernel.h — SimCoupe Circle kernel (multicore + USB gamepad)
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
#include <circle/usb/usbgamepad.h>
#include <circle/multicore.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <vc4/vchiq/vchiqdevice.h>

#define MAX_GAMEPADS 2

enum TShutdownMode { ShutdownNone, ShutdownHalt, ShutdownReboot };

class CKernel : public CMultiCoreSupport
{
public:
    CKernel();
    ~CKernel();

    boolean Initialize();
    TShutdownMode Run();           // Core 0: USB + scheduler
    void Run(unsigned nCore) override;  // Core 1: Z80, Core 2: Sound

    static void GamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState);
    static void GamePadRemovedHandler(CDevice *pDevice, void *pContext);

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
    CSoundBaseDevice   *m_pSound;

    CUSBGamePadDevice  *m_pGamePad[MAX_GAMEPADS];

    volatile bool       m_bLaunch;
};
