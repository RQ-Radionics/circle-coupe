/*
 * src/timer/circle/SDL_circletimer_impl.cpp
 *
 * C++ glue between SDL3 timer backend and Circle CTimer/CScheduler.
 * Exposes extern "C" functions called by SDL_circletimer.c.
 *
 * Circle API:
 *   CTimer::GetClockTicks64() -> microseconds since boot (CLOCKHZ=1000000)
 *                                BUT on AArch32 with USE_PHYSICAL_COUNTER it
 *                                returns raw CNTPCT (19.2 MHz on RPi3), NOT
 *                                scaled to 1 MHz.  We fix that below.
 *   CTimer::Get()->usDelay(n) -> busy-wait n microseconds
 *   CScheduler::Get()->Yield() -> cooperative yield (if scheduler running)
 */

#include <circle/timer.h>
#include <circle/sched/scheduler.h>

extern "C" {

/* Returns microseconds since boot. Used as SDL performance counter and as
 * the backing store for high_resolution_clock via gettimeofday().
 *
 * Bug in Circle AArch32 + USE_PHYSICAL_COUNTER:
 *   GetClockTicks64() returns raw CNTPCT (19.2 MHz on RPi3) instead of
 *   scaling to CLOCKHZ=1 MHz as it does in AArch64.  This makes
 *   high_resolution_clock run 19.2x too fast, breaking all throttling
 *   (Frame::Sync 50fps limiter, Sound::FrameUpdate) and causing the
 *   performance counter to show 0% and the emulator to run in permanent
 *   turbo mode.
 *
 * Fix: read CNTPCT and CNTFRQ directly and scale to microseconds.
 * This matches what the AArch64 path does.
 */
unsigned long long circle_get_clock_ticks64(void)
{
#if AARCH == 32
    /* Read the 64-bit physical counter and its frequency */
    unsigned long nCNTPCTLow, nCNTPCTHigh;
    asm volatile ("mrrc p15, 0, %0, %1, c14"
                  : "=r" (nCNTPCTLow), "=r" (nCNTPCTHigh));
    unsigned long long cntpct = ((unsigned long long)nCNTPCTHigh << 32) | nCNTPCTLow;

    unsigned long cntfrq;
    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq));

    /* Scale to microseconds: cntpct * 1_000_000 / cntfrq
     * On RPi3: cntfrq = 19_200_000 → divides to 1 MHz units */
    return cntpct * 1000000ULL / (unsigned long long)cntfrq;
#else
    return (unsigned long long)CTimer::GetClockTicks64();
#endif
}

/* Delay for ns nanoseconds.
 * Circle usDelay works in microseconds; we round up. */
void circle_delay_ns(unsigned long long ns)
{
    unsigned long long us = (ns + 999ULL) / 1000ULL;
    if (us == 0) {
        us = 1;
    }

    /* If scheduler is active, yield for delays > 1ms to avoid starving
     * other tasks. For short delays use busy-wait via CTimer. */
    CScheduler *pScheduler = CScheduler::Get();
    if (pScheduler != nullptr && us >= 1000) {
        pScheduler->usSleep((unsigned)us);
    } else {
        CTimer::Get()->usDelay((unsigned)us);
    }
}

} /* extern "C" */
