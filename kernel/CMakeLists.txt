# kernel/CMakeLists.txt
#
# The test kernel is built via its own Makefile (uses Circle Rules.mk).
# This CMakeLists.txt exists only to satisfy the parent add_subdirectory().
# To build the kernel:
#
#   cmake --build build --target SDL3-circle   # build SDL3-circle.a first
#   make -C kernel                              # then build kernel8-32.img
#
message(STATUS "Kernel: use 'make -C kernel' after building SDL3-circle")
