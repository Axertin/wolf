cmake_minimum_required(VERSION 3.28)

# version.txt holds the full version, e.g. 0.3.0-beta.3. project() rejects a
# prerelease suffix, so it gets only the numeric part.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" WOLF_VERSION LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" WOLF_VERSION_NUMBER "${WOLF_VERSION}")
project(wolf VERSION ${WOLF_VERSION_NUMBER} LANGUAGES C CXX)

# A clean checkout at the tag wolf-v<version.txt> reports the bare version.
# Every other build appends "+<git describe>", so a dev build never reports
# as a released version. --tags is needed because release tags made through
# the GitHub API are lightweight.
find_package(Git QUIET)
set(WOLF_GIT_DESCRIBE "nogit")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --match "wolf-v*" --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_describe_result)
    if(git_describe_result EQUAL 0)
        set(WOLF_GIT_DESCRIBE "${git_describe}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    # ponytail: the suffix is computed at configure time. Commits, checkouts
    # and staging rewrite the index and so re-run configure; an edit that is
    # not staged yet shows up as -dirty only after the next configure.
    if(EXISTS "${git_dir}/index")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/index")
    endif()
endif()
if(WOLF_GIT_DESCRIBE STREQUAL "wolf-v${WOLF_VERSION}")
    set(WOLF_VERSION_STRING "${WOLF_VERSION}")
else()
    set(WOLF_VERSION_STRING "${WOLF_VERSION}+${WOLF_GIT_DESCRIBE}")
endif()
message(STATUS "WOLF version ${WOLF_VERSION_STRING}")

file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/wolf_version.h" CONTENT [[
#pragma once
#define WOLF_VERSION "@WOLF_VERSION@"
#define WOLF_VERSION_STRING "@WOLF_VERSION_STRING@"
]] @ONLY)
add_library(wolf_version INTERFACE)
target_include_directories(wolf_version INTERFACE "${CMAKE_BINARY_DIR}/generated")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Nothing here uses C++ modules, so skip the per-file dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Every exe and DLL lands in one directory, so the tests find dinput8.dll the
# way the game does: next to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MINGW)
    # Link libc++, libunwind and winpthreads statically, so the built DLLs
    # and exes need nothing from the toolchain at run time.
    add_link_options(-static)
    # WOLF_PDB (set by mingw-release) emits CodeView debug info and has lld
    # write a PDB next to each binary, as MSVC does. Without it, debug info
    # stays DWARF for gdb.
    option(WOLF_PDB "Emit CodeView debug info and PDBs" OFF)
    if(WOLF_PDB)
        add_compile_options(-gcodeview)
        add_link_options(-Wl,--pdb=)
    endif()
endif()

# CMake's MSVC defaults turn exceptions on for every target. Each target
# states its own policy instead: the runtime turns them off, and a dependency
# that needs them carries /EHsc on its target.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

include(cmake/deps.cmake)

# WOLF's own targets trim windows.h: no rarely used headers and no min/max macros.
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

# Warnings apply to WOLF's own targets only; deps.cmake has already created
# the third-party ones.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
if(MSVC)
    # C4324 reports the padding an alignas adds, which is what alignas is for.
    add_compile_options(/W4 /utf-8 /wd4324)
else()
    # The rest match what MSVC /W4 warns on and clang leaves off: hidden names, narrowing conversions and unreachable code.
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wshorten-64-to-32 -Wimplicit-int-conversion -Wfloat-conversion -Wunreachable-code)
endif()

add_subdirectory(sdk)
add_subdirectory(runtime)

enable_testing()
add_subdirectory(tests)
