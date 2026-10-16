# Cross toolchain for llvm-mingw (x86_64, UCRT). The install root comes from
# the LLVM_MINGW_ROOT environment variable and defaults to /opt/llvm-mingw.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
else()
    set(LLVM_MINGW_ROOT "/opt/llvm-mingw")
endif()

set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")

if(NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "llvm-mingw not found at ${LLVM_MINGW_ROOT}; set LLVM_MINGW_ROOT")
endif()

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
