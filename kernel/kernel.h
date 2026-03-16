//
// kernel.h
//
// circle-coupe test kernel: validates SDL3-Circle integration
// on Raspberry Pi 3B bare-metal (AArch32).
//
// Tests:
//   1. Video   - CBcmFrameBuffer via SDL3 renderer (colour pattern)
//   2. Audio   - CPWMSoundBaseDevice via SDL3 audio (440 Hz sine tone)
//   3. Input   - USB HID keyboard/mouse via SDL3 events (print to serial)
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/types.h>

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel
{
public:
    CKernel (void);
    ~CKernel (void);

    boolean Initialize (void);
    TShutdownMode Run (void);

private:
    void RunSDL3Test (void);

private:
    // Circle subsystems - ORDER MATTERS (construction/destruction)
    CActLED             m_ActLED;
    CKernelOptions      m_Options;
    CDeviceNameService  m_DeviceNameService;
    CScreenDevice       m_Screen;
    CSerialDevice       m_Serial;
    CExceptionHandler   m_ExceptionHandler;
    CInterruptSystem    m_Interrupt;
    CTimer              m_Timer;
    CLogger             m_Logger;
    CScheduler          m_Scheduler;
    CUSBHCIDevice       m_USBHCI;
};

#endif // _kernel_h
