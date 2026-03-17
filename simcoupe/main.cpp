//
// simcoupe/main.cpp - Circle bare-metal entry point for SimCoupe
//
#include "kernel.h"
#include <circle/startup.h>

int main(void)
{
    CKernel Kernel;
    if (!Kernel.Initialize()) {
        halt();
        return EXIT_HALT;
    }
    TShutdownMode mode = Kernel.Run();
    switch (mode) {
    case ShutdownReboot: reboot(); return EXIT_REBOOT;
    case ShutdownHalt:
    default:             halt();  return EXIT_HALT;
    }
}
