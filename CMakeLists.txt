cmake_minimum_required(VERSION 3.21)

include(cmake/compiler_setup.cmake)

project("wolf" VERSION 0.1.0 LANGUAGES CXX)

# Allow version override from command line or CI
if(DEFINED WOLF_VERSION)
    # Parse version string (remove 'v' prefix if present)
    string(REGEX REPLACE "^v" "" VERSION_CLEAN ${WOLF_VERSION})
else()
    set(VERSION_CLEAN ${CMAKE_PROJECT_VERSION})
endif()

string(REPLACE "." ";" VERSION_LIST ${VERSION_CLEAN})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})


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

# Add the flattening target to the build
add_flatten_target()

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

# Optional: Build tests
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
