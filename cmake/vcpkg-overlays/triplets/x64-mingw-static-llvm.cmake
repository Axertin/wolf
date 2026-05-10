set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH PATH LLVM_MINGW_ROOT)

set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

# Force vcpkg to build dependencies with llvm-mingw (clang + libc++ for the
# mingw target) instead of the system mingw-w64 GCC + libstdc++. Without
# this, vcpkg's binary cache returns the same x64-mingw-static artifact for
# both linux-cross-debug and linux-cross-llvm-debug, producing a libstdc++
# vs libc++ ABI mismatch at link time.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../toolchains/llvm-mingw.cmake")
