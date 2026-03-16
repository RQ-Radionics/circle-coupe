add_library(SDL3-circle-thread STATIC
    circle/SDL_circlethread.c
)
target_include_directories(SDL3-circle-thread PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CIRCLE_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)
