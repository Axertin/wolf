#include "memory_access.h"

#include "mod_lifecycle.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

#include <psapi.h>

// Global memory watching storage
std::mutex g_WatchMutex;
std::vector<std::unique_ptr<MemoryWatch>> g_MemoryWatches;

// Helper function
bool isValidAddress(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void *>(address), &mbi, sizeof(mbi)) == 0)
        return false;

    // Include PAGE_WRITECOPY (0x08) and PAGE_EXECUTE_WRITECOPY (0x80) for Wine/Proton compatibility
    constexpr DWORD readableProtections = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    return (mbi.State == MEM_COMMIT) && (mbi.Protect & readableProtections);
}

// C API implementations
extern "C"
{
    uintptr_t wolfRuntimeGetModuleBase(const char *module_name)
    {
        if (!module_name)
            return 0;

        HMODULE hModule = GetModuleHandleA(module_name);
        return reinterpret_cast<uintptr_t>(hModule);
    }

    int wolfRuntimeIsValidAddress(uintptr_t address)
    {
        return isValidAddress(address) ? 1 : 0;
    }

    int wolfRuntimeReadMemory(uintptr_t address, void *buffer, size_t size)
    {
        if (!buffer || size == 0 || !isValidAddress(address))
            return 0;

        memcpy(buffer, reinterpret_cast<void *>(address), size);
        return 1;
    }

    int wolfRuntimeWriteMemory(uintptr_t address, const void *buffer, size_t size)
    {
        if (!buffer || size == 0 || !isValidAddress(address))
            return 0;

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void *>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return 0;

        memcpy(reinterpret_cast<void *>(address), buffer, size);
        VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &oldProtect);
        return 1;
    }

    void wolfRuntimeFindPattern(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata)
    {
        if (!pattern || !mask || !callback)
            return;

        uintptr_t baseAddress = 0;
        size_t moduleSize = 0;

        if (module_name)
        {
            HMODULE hModule = GetModuleHandleA(module_name);
            if (!hModule)
                return;

            baseAddress = reinterpret_cast<uintptr_t>(hModule);

            MODULEINFO modInfo;
            if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
                return;

            moduleSize = modInfo.SizeOfImage;
        }
        else
        {
            // Search all modules - simplified implementation
            HMODULE hModule = GetModuleHandleA(nullptr); // Main executable
            if (!hModule)
                return;

            baseAddress = reinterpret_cast<uintptr_t>(hModule);

            MODULEINFO modInfo;
            if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
                return;

            moduleSize = modInfo.SizeOfImage;
        }

        size_t patternLength = strlen(mask);
        const uint8_t *searchData = reinterpret_cast<const uint8_t *>(baseAddress);

        for (size_t i = 0; i <= moduleSize - patternLength; ++i)
        {
            bool found = true;
            for (size_t j = 0; j < patternLength; ++j)
            {
                if (mask[j] == 'x' && searchData[i + j] != static_cast<uint8_t>(pattern[j]))
                {
                    found = false;
                    break;
                }
            }

            if (found)
            {
                callback(baseAddress + i, userdata);
            }
        }
    }

    int wolfRuntimeWatchMemory(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description)
    {
        if (!callback || size == 0 || !isValidAddress(start))
            return 0;

        std::lock_guard<std::mutex> lock(g_WatchMutex);

        auto watch = std::make_unique<MemoryWatch>();
        watch->modId = mod_id;
        watch->start = start;
        watch->size = size;
        watch->callback = callback;
        watch->userdata = userdata;
        watch->description = description ? description : "";

        // Take initial snapshot
        watch->lastData.resize(size);
        if (!wolfRuntimeReadMemory(start, watch->lastData.data(), size))
        {
            return 0;
        }

        g_MemoryWatches.push_back(std::move(watch));
        return 1;
    }

    int wolfRuntimeUnwatchMemory(WolfModId mod_id, uintptr_t start)
    {
        std::lock_guard<std::mutex> lock(g_WatchMutex);

        auto it = std::remove_if(g_MemoryWatches.begin(), g_MemoryWatches.end(),
                                 [mod_id, start](const std::unique_ptr<MemoryWatch> &watch) { return watch->modId == mod_id && watch->start == start; });

        if (it != g_MemoryWatches.end())
        {
            g_MemoryWatches.erase(it, g_MemoryWatches.end());
            return 1;
        }

        return 0;
    }

} // extern "C"

namespace wolf::runtime::internal
{

void processMemoryWatches()
{
    std::lock_guard<std::mutex> lock(g_WatchMutex);

    for (const auto &watch : g_MemoryWatches)
    {
        std::vector<uint8_t> currentData(watch->size);
        if (wolfRuntimeReadMemory(watch->start, currentData.data(), watch->size))
        {
            if (currentData != watch->lastData)
            {
                // Memory changed - call callback
                g_CurrentModId = watch->modId;
                try
                {
                    watch->callback(watch->start, watch->lastData.data(), currentData.data(), watch->size, watch->userdata);
                }
                catch (...)
                {
                    // Continue monitoring other watches
                }

                // Update last known data
                watch->lastData = currentData;
            }
        }
    }

    g_CurrentModId = 0;
}

} // namespace wolf::runtime::internal
