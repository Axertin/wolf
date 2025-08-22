#pragma once

// Macros to suppress FARPROC cast warnings - this is expected behavior for Win32 API
#if defined(__clang__)
#define SUPPRESS_FARPROC_CAST_START _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wcast-function-type-mismatch\"")
#define SUPPRESS_FARPROC_CAST_END _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define SUPPRESS_FARPROC_CAST_START __pragma(warning(push)) __pragma(warning(disable : 4191))
#define SUPPRESS_FARPROC_CAST_END __pragma(warning(pop))
#elif defined(__GNUC__)
#define SUPPRESS_FARPROC_CAST_START _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")
#define SUPPRESS_FARPROC_CAST_END _Pragma("GCC diagnostic pop")
#else
#define SUPPRESS_FARPROC_CAST_START
#define SUPPRESS_FARPROC_CAST_END
#endif

bool IsModded();
void LoadMods();
void UnloadMods();
