cmake_minimum_required(VERSION 3.21)

include(cmake/compiler_setup.cmake)

# Read version from version.txt
file(READ "${CMAKE_SOURCE_DIR}/version.txt" VERSION_FROM_FILE)
string(STRIP "${VERSION_FROM_FILE}" VERSION_CLEAN)

# Parse semantic version
include(cmake/parse_version.cmake)
parse_semantic_version(${VERSION_CLEAN})

# Set project version (CMake only understands major.minor.patch)
project("wolf" VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH} LANGUAGES CXX)

# Cross-compilation detection  
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(CROSS_COMPILING_WINDOWS TRUE)
  message(STATUS "Cross-compiling for Windows from ${CMAKE_HOST_SYSTEM_NAME}")
else()
  set(CROSS_COMPILING_WINDOWS FALSE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CROSSCOMPILING)
  set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable vcpkg app-local dependencies" FORCE)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "d:/SteamLibrary/steamapps/common/Okami")
endif()

if(DEFINED ENV{OKAMI_PATH})
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "$ENV{OKAMI_PATH}")
endif()

set(CXX_STD "23" CACHE STRING "Requested C++ standard")
set(STD_FEATURE "cxx_std_${CXX_STD}")

find_package(minhook CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

# Generate versioned wolf_version.h from template
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/wolf_version.h.in"
    "${CMAKE_BINARY_DIR}/include/wolf_version.h"
    @ONLY
)

# Include API flattening system
include(cmake/flatten_api.cmake)

# Include gamestate header generation
include(cmake/generate_gamestate.cmake)

# Add the flattening target to the build
add_flatten_target()

# Generate gamestate headers during configure
generate_gamestate_headers()

add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_win32.cpp
)

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends)
target_compile_features(imgui PRIVATE ${STD_FEATURE})

add_subdirectory(src/runtime) # dinput8.dll
add_subdirectory(src/loader) # wolf-loader.exe

# Optional: Build devtools mod
add_subdirectory(src/devtools)

# Optional: Build tests
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
