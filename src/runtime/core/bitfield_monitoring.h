#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wolf_types.h"

// Bitfield monitor implementation structure
struct WolfBitfieldMonitorImpl
{
    WolfModId modId;                     ///< ID of the mod that owns this monitor
    uintptr_t address;                   ///< Memory address being monitored
    size_t sizeInBytes;                  ///< Size of the bitfield in bytes
    WolfBitfieldChangeCallback callback; ///< Callback to invoke on changes
    void *userdata;                      ///< User data passed to callback
    std::string description;             ///< Human-readable description
    std::vector<uint8_t> previousData;   ///< Previous state for comparison
    bool initialized;                    ///< Whether we have baseline data

    WolfBitfieldMonitorImpl(WolfModId id, uintptr_t addr, size_t size, WolfBitfieldChangeCallback cb, void *ud, const char *desc)
        : modId(id), address(addr), sizeInBytes(size), callback(cb), userdata(ud), description(desc ? desc : ""), previousData(size, 0), initialized(false)
    {
    }
};

// Global bitfield monitoring storage (defined in bitfield_monitoring.cpp)
extern std::vector<std::unique_ptr<WolfBitfieldMonitorImpl>> g_BitfieldMonitors;
extern std::mutex g_BitfieldMonitorsMutex;

// Helper functions
void processBitFieldMonitors();

// Internal functions for bitfield monitoring (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
void processBitFieldMonitors();
} // namespace wolf::runtime::internal

// C API functions for bitfield monitoring
extern "C"
{
    WolfBitfieldMonitorHandle __cdecl wolfRuntimeCreateBitfieldMonitor(WolfModId mod_id, uintptr_t address, size_t size_in_bytes,
                                                                       WolfBitfieldChangeCallback callback, void *userdata, const char *description);
    WolfBitfieldMonitorHandle __cdecl wolfRuntimeCreateBitfieldMonitorModule(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                             WolfBitfieldChangeCallback callback, void *userdata, const char *description);
    void __cdecl wolfRuntimeDestroyBitfieldMonitor(WolfBitfieldMonitorHandle monitor);
    int __cdecl wolfRuntimeUpdateBitfieldMonitor(WolfBitfieldMonitorHandle monitor);
    int __cdecl wolfRuntimeResetBitfieldMonitor(WolfBitfieldMonitorHandle monitor);
}