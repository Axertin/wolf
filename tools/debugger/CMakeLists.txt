# The Okami debugger: the debugger mod and the wolf-capture reader, which share wolf_dbg. It builds on its own, outside WOLF's build:
#   cmake -S tools/debugger -B build/debugger -G Ninja -DCMAKE_BUILD_TYPE=Release [--toolchain "$PWD/cmake/llvm-mingw.cmake"]
cmake_minimum_required(VERSION 3.28)
project(wolf-debugger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The mod lands in build/debugger/mods/debugger/, ready to copy into the game's mods directory.
set(WOLF_MODS_DIR "${CMAKE_BINARY_DIR}/mods")
include(../../sdk/cmake/WolfMod.cmake)

add_library(wolf_dbg STATIC capture.cpp config.cpp decode.cpp session.cpp)
target_include_directories(wolf_dbg PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(wolf-capture reader.cpp)
target_link_libraries(wolf-capture PRIVATE wolf_dbg)
if(MINGW)
    # A plain Windows shell has no libc++ to load either.
    target_link_options(wolf-capture PRIVATE -static)
endif()

wolf_add_mod(debugger SOURCES debugger.cpp FILES mod.ini configs/an03-registry.cfg configs/bootset-window.cfg configs/softreset-heap.cfg)
target_link_libraries(debugger PRIVATE wolf_dbg)
# Every tool shares one version, in tools/version.txt.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../version.txt" WOLF_TOOLS_VERSION LIMIT_COUNT 1)
target_compile_definitions(debugger PRIVATE WOLF_TOOLS_VERSION="${WOLF_TOOLS_VERSION}")
