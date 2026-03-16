add_library(SDL3-circle-video STATIC
    circle/SDL_circlevideo.c
)
target_include_directories(SDL3-circle-video PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CIRCLE_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)
