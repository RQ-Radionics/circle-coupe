/*
 * src/time/circle/SDL_circlesystime.c
 *
 * SDL3 system time backend for Circle bare-metal (RPi 3B).
 *
 * Circle has no RTC by default on RPi 3B (no battery-backed clock).
 * SDL_GetCurrentTime returns 0 (epoch 1970-01-01).
 * When a Circle RTC addon is available this can be extended.
 */
#include "SDL_internal.h"

#ifdef SDL_TIME_CIRCLE

#include "time/SDL_time_c.h"

void SDL_GetSystemTimeLocalePreferences(SDL_DateFormat *df, SDL_TimeFormat *tf)
{
    /* No locale info on bare-metal; default to ISO 8601 / 24h */
    if (df) *df = SDL_DATE_FORMAT_YYYYMMDD;
    if (tf) *tf = SDL_TIME_FORMAT_24HR;
}

bool SDL_GetCurrentTime(SDL_Time *ticks)
{
    if (!ticks) {
        return SDL_InvalidParamError("ticks");
    }
    /* No RTC: return 0 (Unix epoch). A real RTC addon can be added later. */
    *ticks = 0;
    return true;
}

bool SDL_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool localTime)
{
    if (!dt) {
        return SDL_InvalidParamError("dt");
    }
    /* Stub: always return epoch */
    (void)ticks;
    (void)localTime;
    dt->year        = 1970;
    dt->month       = 1;
    dt->day         = 1;
    dt->hour        = 0;
    dt->minute      = 0;
    dt->second      = 0;
    dt->nanosecond  = 0;
    dt->day_of_week = 4; /* Thursday */
    dt->utc_offset  = 0;
    return true;
}

#endif /* SDL_TIME_CIRCLE */
