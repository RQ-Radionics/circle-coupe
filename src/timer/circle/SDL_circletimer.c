/*
 * src/timer/circle/SDL_circletimer.c
 *
 * SDL3 timer backend for Circle bare-metal (RPi 3B AArch32).
 *
 * Circle CTimer provides:
 *   GetClockTicks64() -> 64-bit microsecond counter at 1 MHz (CLOCKHZ)
 *
 * SDL3 performance counter API:
 *   SDL_GetPerformanceCounter()   -> raw ticks (microseconds)
 *   SDL_GetPerformanceFrequency() -> 1000000 (CLOCKHZ)
 *   SDL_SYS_DelayNS(ns)          -> delay in nanoseconds
 */
#include "SDL_internal.h"

#ifdef SDL_TIMER_CIRCLE

#include "timer/SDL_timer_c.h"

/* Implemented in SDL_circletimer_impl.cpp (C++ glue to Circle CTimer) */
extern unsigned long long circle_get_clock_ticks64(void);
extern void circle_delay_ns(unsigned long long ns);

/* Circle CTimer runs at 1 MHz (CLOCKHZ = 1000000).
 * GetClockTicks64() returns microseconds since boot. */
#define CIRCLE_CLOCKHZ  1000000ULL

Uint64 SDL_GetPerformanceCounter(void)
{
    return (Uint64)circle_get_clock_ticks64();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return CIRCLE_CLOCKHZ;
}

void SDL_SYS_DelayNS(Uint64 ns)
{
    circle_delay_ns((unsigned long long)ns);
}

#endif /* SDL_TIMER_CIRCLE */
