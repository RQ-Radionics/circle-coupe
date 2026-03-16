/*
 * src/timer/circle/SDL_circletimer_impl.cpp
 *
 * C++ glue between SDL3 timer backend and Circle CTimer/CScheduler.
 * Exposes extern "C" functions called by SDL_circletimer.c.
 *
 * Circle API:
 *   CTimer::GetClockTicks64() -> microseconds since boot (CLOCKHZ=1000000)
 *   CTimer::Get()->usDelay(n) -> busy-wait n microseconds
 *   CScheduler::Get()->Yield() -> cooperative yield (if scheduler running)
 */

#include <circle/timer.h>
#include <circle/sched/scheduler.h>

extern "C" {

/* Returns microseconds since boot. Used as SDL performance counter.
 * Frequency = 1000000 Hz (CLOCKHZ). */
unsigned long long circle_get_clock_ticks64(void)
{
    return (unsigned long long)CTimer::GetClockTicks64();
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
