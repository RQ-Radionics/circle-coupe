/*
 * SDL_build_config.h - Circle bare-metal configuration for SDL3
 *
 * Based on SDL_build_config_minimal.h, adapted for Raspberry Pi 3B
 * running Circle bare-metal framework (AArch32, Cortex-A53).
 */

#ifndef SDL_build_config_h_
#define SDL_build_config_h_

/* Must be included by SDL_internal.h via #include "SDL_build_config.h" */

#include <SDL3/SDL_platform_defines.h>

/* ---- Standard headers available via arm-none-eabi-gcc newlib ---- */
#define HAVE_STDARG_H       1
#define HAVE_STDDEF_H       1
#define HAVE_STDINT_H       1
#define HAVE_STDBOOL_H      1
#define HAVE_FLOAT_H        1
#define HAVE_LIMITS_H       1
#define HAVE_INTTYPES_H     1
#define HAVE_MATH_H         1
#define HAVE_STRING_H       1
#define HAVE_STDLIB_H       1
#define HAVE_STDIO_H        1   /* needed for vsnprintf; fopen guarded separately */
/* sys/types.h not available with -nostdinc; SDL3 guards it with HAVE_SYS_TYPES_H */
/* #define HAVE_SYS_TYPES_H 1 */
#define HAVE_WCHAR_H        1
#define HAVE_MEMORY_H       1
#define HAVE_STRINGS_H      1

/* ---- GCC builtins ---- */
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define HAVE_GCC_ATOMICS                1

/* ---- libc functions available in newlib (arm-none-eabi) ---- */
#define HAVE_MALLOC         1
#define HAVE_FREE           1
#define HAVE_MEMSET         1
#define HAVE_MEMCPY         1
#define HAVE_MEMMOVE        1
#define HAVE_MEMCMP         1
#define HAVE_STRLEN         1
#define HAVE_STRCMP         1
#define HAVE_STRNCMP        1
#define HAVE_STRCHR         1
#define HAVE_STRRCHR        1
#define HAVE_STRSTR         1
#define HAVE_STRPBRK        1
#define HAVE_STRTOL         1
#define HAVE_STRTOUL        1
#define HAVE_STRTOLL        1
#define HAVE_STRTOULL       1
#define HAVE_STRTOD         1
#define HAVE_SSCANF         1
#define HAVE_SNPRINTF       1
#define HAVE_VSNPRINTF      1
#define HAVE_ABS            1
#define HAVE_FABS           1
#define HAVE_FABSF          1
#define HAVE_SIN            1
#define HAVE_SINF           1
#define HAVE_COS            1
#define HAVE_COSF           1
#define HAVE_SQRT           1
#define HAVE_SQRTF          1
#define HAVE_FLOOR          1
#define HAVE_FLOORF         1
#define HAVE_CEIL           1
#define HAVE_CEILF          1
#define HAVE_COPYSIGN       1
#define HAVE_COPYSIGNF      1
#define HAVE_LOG            1
#define HAVE_LOG10          1
#define HAVE_POW            1
#define HAVE_POWF           1
#define HAVE_SCALBN         1
#define HAVE_ATAN           1
#define HAVE_ATAN2          1
#define HAVE_EXP            1
#define HAVE_FMOD           1
#define HAVE_MODF           1
#define HAVE_ROUND          1
#define HAVE_ROUNDF         1
#define HAVE_LROUND         1
#define HAVE_LROUNDF        1
#define HAVE_TRUNC          1
#define HAVE_TRUNCF         1
#define HAVE_ISINF          1
#define HAVE_ISNAN          1
#define HAVE_ISINFF         1
#define HAVE_ISNANF         1

/* setjmp is available in Circle via lib/setjmp.S */
#define HAVE_SETJMP         1

/* ---- NEON ARM SIMD ---- */
#define SDL_NEON_INTRINSICS 1

/* ---- Threads: single-threaded cooperative (Circle CScheduler) ---- */
#define SDL_THREADS_DISABLED 1

/* ---- Video: Circle framebuffer backend ---- */
#define SDL_VIDEO_DRIVER_CIRCLE 1
#define SDL_VIDEO_DRIVER_DUMMY  1   /* fallback */
#define SDL_VIDEO_OPENGL        0
#define SDL_VIDEO_OPENGLES      0
#define SDL_VIDEO_VULKAN        0
#define SDL_VIDEO_METAL         0

/* ---- Render: software renderer only ---- */
#define SDL_VIDEO_RENDER_SW     1

/* ---- Audio: Circle PWM/HDMI backend ---- */
#define SDL_AUDIO_DRIVER_CIRCLE 1
#define SDL_AUDIO_DRIVER_DUMMY  1   /* fallback */

/* ---- Input/Joystick: via Circle USB (handled through events) ---- */
#define SDL_JOYSTICK_VIRTUAL    1
#define SDL_JOYSTICK_DISABLED   0

/* ---- Disabled subsystems ---- */
#define SDL_HAPTIC_DISABLED     1
#define SDL_SENSOR_DISABLED     1
#define SDL_HIDAPI_DISABLED     1
#define SDL_LOADSO_DUMMY        1
#define SDL_CAMERA_DRIVER_DUMMY 1
#define SDL_DIALOG_DUMMY        1
#define SDL_TRAY_DUMMY          1
#define SDL_PROCESS_DUMMY       1

/* ---- Filesystem: Circle FatFs ---- */
#define SDL_FILESYSTEM_CIRCLE   1
#define SDL_FILESYSTEM_DUMMY    1   /* fallback until FatFs implemented */
#define SDL_FSOPS_DUMMY         1

/* ---- Timer: Circle CTimer ---- */
#define SDL_TIMER_CIRCLE        1

/* ---- Misc ---- */
#define SDL_MISC_DISABLED       1

/* ---- Platform ---- */
#define SDL_PLATFORM_CIRCLE     1

/* ---- Bare-metal stdio guards ---- */
/* Skip fstat-based directory test (no sys/stat.h on bare-metal) */
#define SKIP_STDIO_DIR_TEST     1

/* ---- Disable dynamic API (no dlopen on bare-metal) ---- */
#define SDL_DYNAMIC_API         0

#endif /* SDL_build_config_h_ */
