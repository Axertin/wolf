#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wolf_types.h"

// Memory watching structure
struct MemoryWatch
{
    WolfModId modId;
    uintptr_t start;
    size_t size;
    WolfMemoryWatchCallback callback;
    void *userdata;
    std::string description;
    std::vector<uint8_t> lastData;
};

// Global memory watching storage (defined in memory_access.cpp)
extern std::mutex g_WatchMutex;
extern std::vector<std::unique_ptr<MemoryWatch>> g_MemoryWatches;

// Helper functions
bool isValidAddress(uintptr_t address);

// Internal functions for memory access (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
void processMemoryWatches();
} // namespace wolf::runtime::internal

// C API functions for memory access
extern "C"
{
    uintptr_t __cdecl wolfRuntimeGetModuleBase(const char *module_name);
    int __cdecl wolfRuntimeIsValidAddress(uintptr_t address);
    int __cdecl wolfRuntimeReadMemory(uintptr_t address, void *buffer, size_t size);
    int __cdecl wolfRuntimeWriteMemory(uintptr_t address, const void *buffer, size_t size);
    void __cdecl wolfRuntimeFindPattern(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata);
    int __cdecl wolfRuntimeWatchMemory(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata,
                                       const char *description);
    int __cdecl wolfRuntimeUnwatchMemory(WolfModId mod_id, uintptr_t start);
}