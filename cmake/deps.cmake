# Every third-party dependency, fetched as a release tarball pinned by URL and
# SHA256. Downloads land in build/_downloads, which all presets share.
include(FetchContent)

set(WOLF_DOWNLOAD_CACHE "${CMAKE_SOURCE_DIR}/build/_downloads" CACHE PATH "Shared download cache for FetchContent")

# SOURCE_SUBDIR points at a directory that does not exist, so
# FetchContent_MakeAvailable only unpacks and never runs the project's own
# CMakeLists.txt.
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.tar.gz
    URL_HASH SHA256=1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e
    DOWNLOAD_DIR "${WOLF_DOWNLOAD_CACHE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR wolf-no-cmake)

FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/releases/download/v2.5.3/doctest-v2.5.3.tar.gz
    URL_HASH SHA256=eb4a39729d091e43d4d593888bf4d145ee9a0f0e13f7404484b6f2fb60d5d50f
    DOWNLOAD_DIR "${WOLF_DOWNLOAD_CACHE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR wolf-no-cmake)

FetchContent_MakeAvailable(minhook doctest)

# MinHook, x64 only, so hde32.c stays out.
add_library(minhook STATIC
    "${minhook_SOURCE_DIR}/src/buffer.c"
    "${minhook_SOURCE_DIR}/src/hook.c"
    "${minhook_SOURCE_DIR}/src/trampoline.c"
    "${minhook_SOURCE_DIR}/src/hde/hde64.c")
target_include_directories(minhook PUBLIC "${minhook_SOURCE_DIR}/include")

# doctest is one header, included as <doctest/doctest.h>. REQUIRE reports a
# failure by throwing, so everything that links it builds with exceptions.
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
target_compile_options(doctest INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
