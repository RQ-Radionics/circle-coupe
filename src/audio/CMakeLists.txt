# SDL3-circle-audio: Circle PWM sound backend C++ glue
add_library(SDL3-circle-audio STATIC
    circle/SDL_circleaudio_impl.cpp
)
target_include_directories(SDL3-circle-audio PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CMAKE_SOURCE_DIR}/src/include
    ${CIRCLE_INCLUDE_DIRS}
)
target_compile_options(SDL3-circle-audio PRIVATE
    -fno-short-enums
)
