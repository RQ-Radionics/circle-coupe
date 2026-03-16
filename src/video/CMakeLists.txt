# SDL3-circle-video: Circle framebuffer + input backend
# - SDL_circlevideo.c                    : SDL3 VideoBootStrap + SDL_VideoDevice callbacks (C)
# - SDL_circlefb.cpp                     : C++ glue to CBcmFrameBuffer
# - ../events/circle/SDL_circleevents.c  : SDL3 event injection (C)
# - ../events/circle/SDL_circleevents_impl.cpp : USB keyboard+mouse glue (C++)

add_library(SDL3-circle-video STATIC
    circle/SDL_circlevideo.c
    circle/SDL_circlefb.cpp
    ${CMAKE_SOURCE_DIR}/src/events/circle/SDL_circleevents.c
    ${CMAKE_SOURCE_DIR}/src/events/circle/SDL_circleevents_impl.cpp
)

target_include_directories(SDL3-circle-video PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/video
    ${SDL3_SRC}
    ${SDL3_SRC}/video
    ${CIRCLE_INCLUDE_DIRS}
)

target_compile_definitions(SDL3-circle-video PRIVATE
    SDL_VIDEO_DRIVER_CIRCLE=1
)

target_compile_options(SDL3-circle-video PRIVATE
    -fno-short-enums
    -Wno-unused-function
)

# Resolution can be overridden at cmake time
if(DEFINED CIRCLE_SCREEN_WIDTH)
    target_compile_definitions(SDL3-circle-video PRIVATE
        CIRCLE_SCREEN_WIDTH=${CIRCLE_SCREEN_WIDTH}
        CIRCLE_SCREEN_HEIGHT=${CIRCLE_SCREEN_HEIGHT}
    )
endif()
