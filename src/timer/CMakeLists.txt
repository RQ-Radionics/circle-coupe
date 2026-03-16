# SDL3-circle-timer: C++ glue to Circle CTimer (must be compiled as C++)
add_library(SDL3-circle-timer STATIC
    circle/SDL_circletimer_impl.cpp
)
target_include_directories(SDL3-circle-timer PRIVATE
    ${SDL3_INC}
    ${SDL3_INC}/SDL3
    ${SDL3_SRC}
    ${CMAKE_SOURCE_DIR}/src/include
    ${CIRCLE_INCLUDE_DIRS}
)
# No extra defines needed - the .cpp just wraps Circle C++ API as extern "C"
