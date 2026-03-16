/*
 * src/SDL_memset4_circle.c
 *
 * Provides only SDL_memset4() for Circle bare-metal build.
 * SDL_memset.c is excluded from the build because it also defines memset/memcpy
 * which conflicts with Circle/newlib. SDL_memset4 is SDL3-specific and not in newlib.
 */
#include "SDL_internal.h"

void *SDL_memset4(void *dst, Uint32 val, size_t dwords)
{
    size_t _n = (dwords + 3) / 4;
    Uint32 *_p = (Uint32 *)dst;
    Uint32 _val = val;
    if (dwords == 0) {
        return dst;
    }
    switch (dwords % 4) {
    case 0:
        do {
            *_p++ = _val;
            /* fallthrough */
        case 3:
            *_p++ = _val;
            /* fallthrough */
        case 2:
            *_p++ = _val;
            /* fallthrough */
        case 1:
            *_p++ = _val;
        } while (--_n);
    }
    return dst;
}
