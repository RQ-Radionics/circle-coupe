# cmake/sdl3-circle-options.cmake
#
# SDL3 CMake cache presets for Circle bare-metal build.
# Include this BEFORE add_subdirectory(SDL3).
#

# --- Build type ---
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY  OFF CACHE BOOL "" FORCE)

# --- Disable OS-dependent features ---
set(SDL_LIBC          OFF CACHE BOOL "" FORCE)   # no system libc
set(SDL_PTHREADS      OFF CACHE BOOL "" FORCE)   # no pthreads
set(SDL_CLOCK_GETTIME OFF CACHE BOOL "" FORCE)   # no POSIX clock
set(SDL_GCC_ATOMICS   ON  CACHE BOOL "" FORCE)   # gcc builtins OK
set(SDL_ASSEMBLY      ON  CACHE BOOL "" FORCE)   # NEON asm OK
set(SDL_RPATH         OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL       OFF CACHE BOOL "" FORCE)

# --- Disable subsystems not available bare-metal ---
set(SDL_GPU           OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA        OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC        OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR        OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG        OFF CACHE BOOL "" FORCE)
set(SDL_TRAY          OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI        OFF CACHE BOOL "" FORCE)
set(SDL_POWER         OFF CACHE BOOL "" FORCE)

# --- Keep these: we implement them for Circle ---
set(SDL_AUDIO         ON  CACHE BOOL "" FORCE)
set(SDL_VIDEO         ON  CACHE BOOL "" FORCE)
set(SDL_RENDER        ON  CACHE BOOL "" FORCE)
set(SDL_EVENTS        ON  CACHE BOOL "" FORCE)
set(SDL_JOYSTICK      ON  CACHE BOOL "" FORCE)

# --- Disable all platform-specific backends (we provide circle ones) ---
# Video
set(SDL_OPENGL        OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES      OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN        OFF CACHE BOOL "" FORCE)
set(SDL_METAL         OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTX       OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN     OFF CACHE BOOL "" FORCE)
set(SDL_X11           OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND       OFF CACHE BOOL "" FORCE)
set(SDL_KMSDRM        OFF CACHE BOOL "" FORCE)
set(SDL_RPI           OFF CACHE BOOL "" FORCE)   # old dispmanx backend
set(SDL_VIVANTE       OFF CACHE BOOL "" FORCE)
# Audio
set(SDL_ALSA          OFF CACHE BOOL "" FORCE)
set(SDL_PULSEAUDIO    OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE      OFF CACHE BOOL "" FORCE)
set(SDL_JACK          OFF CACHE BOOL "" FORCE)
set(SDL_OSS           OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO         OFF CACHE BOOL "" FORCE)
set(SDL_COREAUDIO     OFF CACHE BOOL "" FORCE)
set(SDL_WASAPI        OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTSOUND   OFF CACHE BOOL "" FORCE)
# Dynamic loading
set(SDL_LOADSO        OFF CACHE BOOL "" FORCE)
# Misc
set(SDL_DBUS          OFF CACHE BOOL "" FORCE)
set(SDL_IBUS          OFF CACHE BOOL "" FORCE)
set(SDL_FCITX         OFF CACHE BOOL "" FORCE)
set(SDL_SYSTEM_ICONV  OFF CACHE BOOL "" FORCE)
