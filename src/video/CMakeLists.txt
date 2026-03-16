# SDL3-circle-video: Circle framebuffer backend
# - SDL_circlevideo.c  : SDL3 VideoBootStrap + SDL_VideoDevice callbacks (C)
# - SDL_circlefb.cpp   : C++ glue to CBcmFrameBuffer (Circle API)

add_library(SDL3-circle-video STATIC
    circle/SDL_circlevideo.c
    circle/SDL_circlefb.cpp
)

target_include_directories(SDL3-circle-video PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/video      # for SDL_video_circle.c to find our header
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
