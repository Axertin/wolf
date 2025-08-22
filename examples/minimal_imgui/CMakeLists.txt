cmake_minimum_required(VERSION 3.20)

# Example mod: Minimal ImGui Test
project(minimal_imgui_mod VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ImGui source files
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# Create ImGui as a static library for this mod
add_library(imgui_mod_static STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_mod_static PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui_mod_static PUBLIC d3d11 dxgi)

# Create the mod as a shared library
add_library(minimal_imgui_mod SHARED minimal_imgui.cpp)

# Include directories
target_include_directories(minimal_imgui_mod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/api
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link the mod against ImGui and Wolf framework
target_link_libraries(minimal_imgui_mod PRIVATE imgui_mod_static)

# Set C++ standard for the mod
target_compile_features(minimal_imgui_mod PRIVATE cxx_std_17)
