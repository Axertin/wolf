# Builds on its own, as a mod author's project would:
#   cmake -S examples/hello_world -B build/hello_world -G Ninja [--toolchain "$PWD/cmake/llvm-mingw.cmake"]
cmake_minimum_required(VERSION 3.28)
project(hello_world LANGUAGES CXX)

# The mod lands in build/hello_world/mods/hello_world/, ready to copy into the game's mods directory.
set(WOLF_MODS_DIR "${CMAKE_BINARY_DIR}/mods")
include(../../sdk/cmake/WolfMod.cmake)

wolf_add_mod(hello_world SOURCES hello_world.cpp FILES mod.ini config.ini)
