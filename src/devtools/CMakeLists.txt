# DevTools - Optional WOLF Mod for Game Investigation
# Independent devtools mod for monitoring and debugging Okami game state

cmake_minimum_required(VERSION 3.21)

# Make devtools an optional build target
option(BUILD_DEVTOOLS "Build DevTools mod" OFF)

if(NOT BUILD_DEVTOOLS)
    return()
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTARGET_NAME=devtools
    -DSEARCH_DIR=src/devtools
    -P ${CMAKE_SOURCE_DIR}/cmake/generate_sources.cmake
)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

# Create the devtools shared library
add_library(devtools SHARED ${devtools_SOURCES})
target_compile_features(devtools PUBLIC ${STD_FEATURE})
target_include_directories(devtools PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/api
)

# Link required libraries
target_link_libraries(devtools PRIVATE
    yaml-cpp::yaml-cpp imgui
)

target_link_libraries(devtools PRIVATE d3d11 dxgi)

enable_strict_warnings(devtools)
apply_release_optimizations(devtools)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_link_libraries(devtools PRIVATE Crypt32)
    target_compile_options(devtools PRIVATE /Zc:__cplusplus)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CROSS_COMPILING_WINDOWS)
    target_link_libraries(devtools PRIVATE dwmapi d3dcompiler)
    target_compile_options(devtools PRIVATE -Wa,-mbig-obj -g1)
    target_link_options(devtools PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
        LINKER:--allow-multiple-definition
        LINKER:--enable-stdcall-fixup)
endif()

# Install YAML game data files
install(DIRECTORY game-data/
    DESTINATION mods/devtools/game-data/
)

# Install to Okami directory structure
install(TARGETS devtools
    DESTINATION mods/devtools/
)
