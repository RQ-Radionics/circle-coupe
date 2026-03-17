# src/CMakeLists.txt
#
# Builds SDL3 as a static library for Circle bare-metal (RPi3 AArch32).
# We compile SDL3 sources directly - bypassing SDL3's own CMakeLists.txt
# which requires a recognised OS/thread model.
#
# Backends implemented in this repo (src/video/circle, src/audio/circle, etc.)
# are compiled alongside the SDL3 core sources.

set(SDL3_SRC "${CMAKE_SOURCE_DIR}/SDL3/src")
set(SDL3_INC "${CMAKE_SOURCE_DIR}/SDL3/include")

# ---------------------------------------------------------------------------
# SDL3 core sources (platform-independent)
# ---------------------------------------------------------------------------
set(SDL3_CORE_SOURCES
    # Core SDL
    ${SDL3_SRC}/SDL.c
    ${SDL3_SRC}/SDL_assert.c
    ${SDL3_SRC}/stdlib/SDL_murmur3.c
    ${SDL3_SRC}/SDL_error.c
    ${SDL3_SRC}/SDL_guid.c
    ${SDL3_SRC}/SDL_hashtable.c
    ${SDL3_SRC}/SDL_hints.c
    ${SDL3_SRC}/SDL_list.c
    ${SDL3_SRC}/SDL_log.c
    ${SDL3_SRC}/SDL_properties.c
    ${SDL3_SRC}/SDL_utils.c
    # Atomic
    ${SDL3_SRC}/atomic/SDL_atomic.c
    ${SDL3_SRC}/atomic/SDL_spinlock.c
    # Events
    ${SDL3_SRC}/events/SDL_clipboardevents.c
    ${SDL3_SRC}/events/SDL_displayevents.c
    ${SDL3_SRC}/events/SDL_dropevents.c
    ${SDL3_SRC}/events/SDL_events.c
    ${SDL3_SRC}/events/SDL_eventwatch.c
    ${SDL3_SRC}/events/SDL_keyboard.c
    ${SDL3_SRC}/events/SDL_keymap.c
    ${SDL3_SRC}/events/SDL_mouse.c
    ${SDL3_SRC}/events/SDL_pen.c
    ${SDL3_SRC}/events/SDL_quit.c
    ${SDL3_SRC}/events/SDL_scancode_tables.c
    ${SDL3_SRC}/events/SDL_touch.c
    ${SDL3_SRC}/events/SDL_windowevents.c
    # Stdlib (SDL's internal implementations)
    # SDL_memcpy/memmove/memset excluded - conflicts with Circle/newlib.
    # SDL_memset4 provided via our own stub (only function needed, not in newlib).
    ${CMAKE_SOURCE_DIR}/src/SDL_memset4_circle.c
    ${SDL3_SRC}/stdlib/SDL_crc16.c
    ${SDL3_SRC}/stdlib/SDL_crc32.c
    ${SDL3_SRC}/stdlib/SDL_getenv.c
    ${SDL3_SRC}/stdlib/SDL_iconv.c
    ${SDL3_SRC}/stdlib/SDL_malloc.c
    ${SDL3_SRC}/stdlib/SDL_mslibc.c
    ${SDL3_SRC}/stdlib/SDL_murmur3.c
    ${SDL3_SRC}/stdlib/SDL_qsort.c
    ${SDL3_SRC}/stdlib/SDL_random.c
    ${SDL3_SRC}/stdlib/SDL_stdlib.c
    ${SDL3_SRC}/stdlib/SDL_string.c
    ${SDL3_SRC}/stdlib/SDL_strtokr.c
    # Libm (SDL's internal math - no -lm needed)
    ${SDL3_SRC}/libm/e_atan2.c
    ${SDL3_SRC}/libm/e_exp.c
    ${SDL3_SRC}/libm/e_fmod.c
    ${SDL3_SRC}/libm/e_log.c
    ${SDL3_SRC}/libm/e_log10.c
    ${SDL3_SRC}/libm/e_pow.c
    ${SDL3_SRC}/libm/e_rem_pio2.c
    ${SDL3_SRC}/libm/e_sqrt.c
    ${SDL3_SRC}/libm/k_cos.c
    ${SDL3_SRC}/libm/k_rem_pio2.c
    ${SDL3_SRC}/libm/k_sin.c
    ${SDL3_SRC}/libm/k_tan.c
    ${SDL3_SRC}/libm/s_atan.c
    ${SDL3_SRC}/libm/s_copysign.c
    ${SDL3_SRC}/libm/s_cos.c
    ${SDL3_SRC}/libm/s_fabs.c
    ${SDL3_SRC}/libm/s_floor.c
    ${SDL3_SRC}/libm/s_modf.c
    ${SDL3_SRC}/libm/s_scalbn.c
    ${SDL3_SRC}/libm/s_sin.c
    ${SDL3_SRC}/libm/s_tan.c
    # Thread - generic (single-threaded stubs, cooperative via Circle scheduler)
    ${SDL3_SRC}/thread/generic/SDL_syscond.c
    ${SDL3_SRC}/thread/generic/SDL_sysmutex.c
    ${SDL3_SRC}/thread/generic/SDL_sysrwlock.c
    ${SDL3_SRC}/thread/generic/SDL_syssem.c
    ${SDL3_SRC}/thread/generic/SDL_systhread.c
    ${SDL3_SRC}/thread/generic/SDL_systls.c
    ${SDL3_SRC}/thread/SDL_thread.c
    # Timer - SDL3 core + Circle platform implementation
    ${SDL3_SRC}/timer/SDL_timer.c
    ${CMAKE_SOURCE_DIR}/src/timer/circle/SDL_circletimer.c
    # Video core
    ${SDL3_SRC}/video/SDL_blit.c
    ${SDL3_SRC}/video/SDL_blit_0.c
    ${SDL3_SRC}/video/SDL_blit_1.c
    ${SDL3_SRC}/video/SDL_blit_A.c
    ${SDL3_SRC}/video/SDL_blit_auto.c
    ${SDL3_SRC}/video/SDL_blit_copy.c
    ${SDL3_SRC}/video/SDL_blit_N.c
    ${SDL3_SRC}/video/SDL_blit_slow.c
    ${SDL3_SRC}/video/SDL_bmp.c
    ${SDL3_SRC}/video/SDL_clipboard.c
    ${SDL3_SRC}/video/SDL_egl.c
    ${SDL3_SRC}/video/SDL_fillrect.c
    ${SDL3_SRC}/video/SDL_pixels.c
    ${SDL3_SRC}/video/SDL_rect.c
    ${SDL3_SRC}/video/SDL_RLEaccel.c
    ${SDL3_SRC}/video/SDL_rotate.c
    ${SDL3_SRC}/video/SDL_stb.c
    ${SDL3_SRC}/video/SDL_stretch.c
    ${SDL3_SRC}/video/SDL_surface.c
    # SDL_video.c replaced by patched copy that registers CIRCLE_bootstrap
    ${CMAKE_SOURCE_DIR}/src/video/SDL_video_circle.c
    ${SDL3_SRC}/video/SDL_video_unsupported.c
    # Dummy video driver (fallback bootstrap)
    ${SDL3_SRC}/video/dummy/SDL_nullvideo.c
    ${SDL3_SRC}/video/dummy/SDL_nullevents.c
    ${SDL3_SRC}/video/dummy/SDL_nullframebuffer.c
    ${SDL3_SRC}/video/SDL_vulkan_utils.c
    ${SDL3_SRC}/video/SDL_yuv.c
    ${SDL3_SRC}/video/yuv2rgb/yuv_rgb_lsx.c
    ${SDL3_SRC}/video/yuv2rgb/yuv_rgb_sse.c
    ${SDL3_SRC}/video/yuv2rgb/yuv_rgb_std.c
    # Audio core (SDL_audio.c patched to add CIRCLEAUDIO_bootstrap)
    ${CMAKE_SOURCE_DIR}/src/audio/SDL_audio_circle.c
    ${CMAKE_SOURCE_DIR}/src/audio/circle/SDL_circleaudio.c
    ${SDL3_SRC}/audio/dummy/SDL_dummyaudio.c
    ${SDL3_SRC}/audio/SDL_audiocvt.c
    ${SDL3_SRC}/audio/SDL_audiodev.c
    ${SDL3_SRC}/audio/SDL_audioqueue.c
    ${SDL3_SRC}/audio/SDL_audioresample.c
    ${SDL3_SRC}/audio/SDL_audiotypecvt.c
    ${SDL3_SRC}/audio/SDL_mixer.c
    ${SDL3_SRC}/audio/SDL_wave.c
    # Render - software only
    ${SDL3_SRC}/render/SDL_render.c
    ${SDL3_SRC}/render/SDL_yuv_sw.c
    ${SDL3_SRC}/render/software/SDL_blendfillrect.c
    ${SDL3_SRC}/render/software/SDL_blendline.c
    ${SDL3_SRC}/render/software/SDL_blendpoint.c
    ${SDL3_SRC}/render/software/SDL_drawline.c
    ${SDL3_SRC}/render/software/SDL_drawpoint.c
    ${SDL3_SRC}/render/software/SDL_render_sw.c
    ${SDL3_SRC}/render/software/SDL_triangle.c
    # CPU info
    ${SDL3_SRC}/cpuinfo/SDL_cpuinfo.c
    # Camera (disabled, but SDL_UpdateCamera() is called from SDL_PumpEvents)
    ${SDL3_SRC}/camera/SDL_camera.c
    ${SDL3_SRC}/camera/dummy/SDL_camera_dummy.c
    # Joystick (dummy driver + virtual; real USB via Circle input events)
    ${SDL3_SRC}/joystick/SDL_joystick.c
    ${SDL3_SRC}/joystick/SDL_gamepad.c
    ${SDL3_SRC}/joystick/SDL_steam_virtual_gamepad.c
    ${SDL3_SRC}/joystick/controller_type.c
    ${SDL3_SRC}/joystick/virtual/SDL_virtualjoystick.c
    ${SDL3_SRC}/joystick/dummy/SDL_sysjoystick.c
    ${SDL3_SRC}/joystick/hidapi/SDL_hidapi_combined.c
    ${SDL3_SRC}/joystick/hidapi/SDL_hidapijoystick.c
    # Sensor (disabled stub)
    ${SDL3_SRC}/sensor/SDL_sensor.c
    ${SDL3_SRC}/sensor/dummy/SDL_dummysensor.c
    # Tray (dummy stub)
    ${SDL3_SRC}/tray/SDL_tray_utils.c
    ${SDL3_SRC}/tray/dummy/SDL_tray.c
    # Power (stub)
    ${SDL3_SRC}/power/SDL_power.c
    # Locale (stub)
    ${SDL3_SRC}/locale/SDL_locale.c
    # Misc - OpenURL stub (no browser on bare metal)
    ${SDL3_SRC}/misc/SDL_url.c
    ${SDL3_SRC}/misc/dummy/SDL_sysurl.c
    # Locale - stub (returns "en_US" via Circle keymap setting)
    ${SDL3_SRC}/locale/SDL_locale.c
    ${SDL3_SRC}/locale/dummy/SDL_syslocale.c
    # Time - SDL3 core + Circle platform implementation
    ${SDL3_SRC}/time/SDL_time.c
    ${CMAKE_SOURCE_DIR}/src/time/circle/SDL_circlesystime.c
    # Filesystem (stub - FatFs added later)
    ${SDL3_SRC}/filesystem/SDL_filesystem.c
    ${SDL3_SRC}/filesystem/dummy/SDL_sysfilesystem.c
    ${SDL3_SRC}/filesystem/dummy/SDL_sysfsops.c
    # IO (SDL_iostream patched to exclude POSIX FD backend on bare-metal)
    ${CMAKE_SOURCE_DIR}/src/SDL_iostream_circle.c
    ${SDL3_SRC}/io/SDL_asyncio.c
    ${SDL3_SRC}/io/generic/SDL_asyncio_generic.c
    # Storage (generic)
    ${SDL3_SRC}/storage/SDL_storage.c
    ${SDL3_SRC}/storage/generic/SDL_genericstorage.c
    # main/generic callbacks
    ${SDL3_SRC}/main/SDL_main_callbacks.c
    ${SDL3_SRC}/main/generic/SDL_sysmain_callbacks.c
)

# ---------------------------------------------------------------------------
# Circle-specific backends (our code in src/)
# ---------------------------------------------------------------------------
add_subdirectory(video)
add_subdirectory(audio)
add_subdirectory(timer)   # builds SDL3-circle-timer (C++ glue to CTimer)
add_subdirectory(thread)  # builds SDL3-circle-thread (placeholder)
# filesystem, misc and time are compiled directly into SDL3-circle core above

# ---------------------------------------------------------------------------
# SDL3-Circle static library
# ---------------------------------------------------------------------------
add_library(SDL3-circle STATIC ${SDL3_CORE_SOURCES})

target_include_directories(SDL3-circle
    PUBLIC
        ${SDL3_INC}
        ${SDL3_INC}/SDL3
        ${CMAKE_SOURCE_DIR}/src/include   # SDL_build_config.h
        ${SDL3_SRC}
    PRIVATE
        ${SDL3_SRC}/video                 # SDL_video_circle.c uses relative includes
        ${SDL3_SRC}/audio                 # SDL_audio_circle.c uses relative includes
        ${SDL3_SRC}/thread                # for thread headers
        ${SDL3_SRC}/io                    # SDL_iostream_circle.c needs SDL_iostream_c.h
        ${CIRCLE_INCLUDE_DIRS}
)

target_compile_options(SDL3-circle PRIVATE
    -fno-short-enums    # SDL3 requires sizeof(enum) == sizeof(int)
    -Wno-unused-function
    -Wno-sign-compare
)

target_compile_definitions(SDL3-circle
    PRIVATE
        SDL_INTERNAL
        SDL_THREADS_DISABLED=1
        # Tell SDL3 we provide our own backends
        SDL_VIDEO_DRIVER_CIRCLE=1
        SDL_AUDIO_DRIVER_CIRCLE=1   # activates CIRCLEAUDIO_bootstrap in SDL_audio_circle.c
        # SDL_DYNAMIC_API=0 is handled via src/include/SDL_dynapi.h override (not -D flag)
        SDL_TIMER_CIRCLE=1      # activates #ifdef SDL_TIMER_CIRCLE in SDL_circletimer.c
        SDL_TIME_CIRCLE=1       # activates #ifdef SDL_TIME_CIRCLE in SDL_circlesystime.c
        SDL_FILESYSTEM_CIRCLE=1
        # Fallback dummy backends (referenced in patched SDL_video/audio_circle.c)
        SDL_VIDEO_DRIVER_DUMMY=1
        SDL_AUDIO_DRIVER_DUMMY=1
        SDL_JOYSTICK_VIRTUAL=1
        SDL_JOYSTICK_DUMMY=1
        SDL_HAPTIC_DISABLED=1
        SDL_SENSOR_DISABLED=1
        SDL_CAMERA_DRIVER_DUMMY=1
        SDL_DIALOG_DUMMY=1
        SDL_TRAY_DUMMY=1
        # Disable backends we don't have
        SDL_VIDEO_OPENGL=0
        SDL_VIDEO_OPENGLES=0
        SDL_VIDEO_VULKAN=0
)

target_link_libraries(SDL3-circle
    PRIVATE
        SDL3-circle-video      # stub (task qjs)
        SDL3-circle-audio      # stub (task 7h3)
        SDL3-circle-timer      # C++ glue to CTimer/CScheduler
        SDL3-circle-thread     # stub (single-threaded)
)
