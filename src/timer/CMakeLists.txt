add_library(SDL3-circle-timer STATIC
    circle/SDL_circletimer.c
)
target_include_directories(SDL3-circle-timer PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CIRCLE_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)
