set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED LLVM_MINGW_ROOT)
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
if(NOT LLVM_MINGW_ROOT)
    set(LLVM_MINGW_ROOT "/opt/llvm-mingw")
endif()

set(CMAKE_C_COMPILER   "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR           "${LLVM_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB       "${LLVM_MINGW_ROOT}/bin/llvm-ranlib")

if(NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "llvm-mingw not found at ${LLVM_MINGW_ROOT}. "
        "Set -DLLVM_MINGW_ROOT=<path> or LLVM_MINGW_ROOT env var, "
        "or install to /opt/llvm-mingw.")
endif()

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")

if(VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

add_definitions(-D_WIN32_WINNT=0x0601)

# llvm-mingw uses lld-link as its linker — emit MS-style PDBs.
# -gcodeview tells clang to emit CodeView debug info (vs DWARF).
# -Wl,/pdb: + -Wl,/debug make lld-link write a PDB next to the binary.
# Applied via INIT vars so they propagate to all targets without per-target wiring.
set(CMAKE_C_FLAGS_INIT   "-g -gcodeview")
set(CMAKE_CXX_FLAGS_INIT "-g -gcodeview")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-Wl,--pdb=")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--pdb=")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-Wl,--pdb=")
