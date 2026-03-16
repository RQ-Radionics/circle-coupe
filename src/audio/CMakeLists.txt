add_library(SDL3-circle-audio STATIC
    circle/SDL_circleaudio.c
)
target_include_directories(SDL3-circle-audio PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CIRCLE_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)
