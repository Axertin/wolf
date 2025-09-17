#include "bitfield_monitoring.h"

#include "../utilities/logger.h"
#include "memory_access.h" // For isValidAddress
#include "mod_lifecycle.h" // For g_CurrentModId

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>

// Global bitfield monitoring storage
std::vector<std::unique_ptr<WolfBitfieldMonitorImpl>> g_BitfieldMonitors;
std::mutex g_BitfieldMonitorsMutex;

// Helper function to process all bitfield monitors
void processBitFieldMonitors()
{
    std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

    for (const auto &monitor : g_BitfieldMonitors)
    {
        if (monitor)
        {
            g_CurrentModId = monitor->modId;
            try
            {
                int result = wolfRuntimeUpdateBitfieldMonitor(reinterpret_cast<WolfBitfieldMonitorHandle>(monitor.get()));
                // if (result > 0)
                // {
                //     ::logDebug("[WOLF] BitField monitor %s detected changes", monitor->description.c_str());
                // }
            }
            catch (...)
            {
                // Continue monitoring other BitField monitors
                ::logError("[WOLF] Exception in BitField monitor %s", monitor->description.c_str());
            }
        }
    }

    g_CurrentModId = 0;
}

// C API implementations
extern "C"
{
    WolfBitfieldMonitorHandle wolfRuntimeCreateBitfieldMonitor(WolfModId mod_id, uintptr_t address, size_t size_in_bytes, WolfBitfieldChangeCallback callback,
                                                               void *userdata, const char *description)
    {
        if (!callback)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: callback is NULL");
            return nullptr;
        }

        if (size_in_bytes == 0 || size_in_bytes > 4096) // Reasonable size limit
        {
            ::logError("[WOLF] Cannot create bitfield monitor: invalid size %zu bytes", size_in_bytes);
            return nullptr;
        }

        if (!isValidAddress(address))
        {
            ::logError("[WOLF] Cannot create bitfield monitor: invalid address 0x%p", reinterpret_cast<void *>(address));
            return nullptr;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            // Reserve capacity to prevent vector reallocation and potential memory corruption
            if (g_BitfieldMonitors.capacity() == g_BitfieldMonitors.size())
            {
                g_BitfieldMonitors.reserve(g_BitfieldMonitors.size() + 16);
            }

            auto monitor = std::make_unique<WolfBitfieldMonitorImpl>(mod_id, address, size_in_bytes, callback, userdata, description);

            WolfBitfieldMonitorImpl *handle = monitor.get();
            g_BitfieldMonitors.push_back(std::move(monitor));

            // ::logDebug("[WOLF] Created bitfield monitor for mod %d at 0x%p (%zu bytes): %s", mod_id, (void *)address, size_in_bytes,
            //            description ? description : "unnamed");

            return reinterpret_cast<WolfBitfieldMonitorHandle>(handle);
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to create bitfield monitor: %s", e.what());
            return nullptr;
        }
    }

    WolfBitfieldMonitorHandle wolfRuntimeCreateBitfieldMonitorModule(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                     WolfBitfieldChangeCallback callback, void *userdata, const char *description)
    {
        if (!module_name)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: module_name is NULL");
            return nullptr;
        }

        HMODULE hModule = GetModuleHandleA(module_name);
        if (!hModule)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: module '%s' not found", module_name);
            return nullptr;
        }

        uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
        uintptr_t address = moduleBase + offset;

        return wolfRuntimeCreateBitfieldMonitor(mod_id, address, size_in_bytes, callback, userdata, description);
    }

    void wolfRuntimeDestroyBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to destroy NULL bitfield monitor handle");
            return;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find and remove the monitor from our vector
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it != g_BitfieldMonitors.end())
            {
                ::logDebug("[WOLF] Destroyed bitfield monitor for mod %d: %s", (*it)->modId, (*it)->description.c_str());
                g_BitfieldMonitors.erase(it);
            }
            else
            {
                ::logWarning("[WOLF] Attempted to destroy invalid bitfield monitor handle");
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to destroy bitfield monitor: %s", e.what());
        }
    }

    int wolfRuntimeUpdateBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to update NULL bitfield monitor handle");
            return 0;
        }

        try
        {
            // NOTE: Mutex should already be held by processBitFieldMonitors caller
            // Removed lock to prevent deadlock

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find the monitor in our vector to validate it
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it == g_BitfieldMonitors.end())
            {
                ::logWarning("[WOLF] Attempted to update invalid bitfield monitor handle");
                return 0;
            }

            WolfBitfieldMonitorImpl *m = it->get();

            // Validate the address is still readable
            if (!isValidAddress(m->address))
            {
                ::logWarning("[WOLF] Bitfield monitor address 0x%p is no longer valid", reinterpret_cast<void *>(m->address));
                return 0;
            }

            // Read current data
            std::vector<uint8_t> currentData(m->sizeInBytes);
            if (!wolfRuntimeReadMemory(m->address, currentData.data(), m->sizeInBytes))
            {
                ::logWarning("[WOLF] Failed to read memory for bitfield monitor at 0x%p", reinterpret_cast<void *>(m->address));
                return 0;
            }

            int changesDetected = 0;

            // Initialize if this is the first check
            if (!m->initialized)
            {
                m->previousData = currentData;
                m->initialized = true;
                return 0; // No changes on first initialization
            }

            // Check for changes bit by bit
            for (size_t byteIndex = 0; byteIndex < m->sizeInBytes; ++byteIndex)
            {
                uint8_t oldByte = m->previousData[byteIndex];
                uint8_t newByte = currentData[byteIndex];

                if (oldByte != newByte)
                {
                    // Check each bit in this byte
                    for (int bitIndex = 0; bitIndex < 8; ++bitIndex)
                    {
                        uint8_t bitMask = static_cast<uint8_t>(1 << bitIndex);
                        int oldBit = (oldByte & bitMask) ? 1 : 0;
                        int newBit = (newByte & bitMask) ? 1 : 0;

                        if (oldBit != newBit)
                        {
                            // Calculate global bit index
                            unsigned int globalBitIndex = static_cast<unsigned int>(byteIndex * 8 + bitIndex);

                            // Call the callback
                            try
                            {
                                m->callback(globalBitIndex, oldBit, newBit, m->userdata);
                                changesDetected++;
                            }
                            catch (...)
                            {
                                ::logError("[WOLF] Exception in bitfield change callback for monitor '%s'", m->description.c_str());
                            }
                        }
                    }
                }
            }

            // Update previous data
            m->previousData = currentData;

            return changesDetected;
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to update bitfield monitor: %s", e.what());
            return 0;
        }
    }

    int wolfRuntimeResetBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to reset NULL bitfield monitor handle");
            return 0;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find the monitor in our vector to validate it
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it == g_BitfieldMonitors.end())
            {
                ::logWarning("[WOLF] Attempted to reset invalid bitfield monitor handle");
                return 0;
            }

            WolfBitfieldMonitorImpl *m = it->get();

            // Validate the address is still readable
            if (!isValidAddress(m->address))
            {
                ::logWarning("[WOLF] Bitfield monitor address 0x%p is no longer valid", reinterpret_cast<void *>(m->address));
                return 0;
            }

            // Read current data and reset baseline
            std::vector<uint8_t> currentData(m->sizeInBytes);
            if (!wolfRuntimeReadMemory(m->address, currentData.data(), m->sizeInBytes))
            {
                ::logWarning("[WOLF] Failed to read memory for bitfield monitor reset at 0x%p", reinterpret_cast<void *>(m->address));
                return 0;
            }

            m->previousData = currentData;
            m->initialized = true;

            ::logDebug("[WOLF] Reset bitfield monitor '%s' at 0x%p", m->description.c_str(), reinterpret_cast<void *>(m->address));
            return 1;
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to reset bitfield monitor: %s", e.what());
            return 0;
        }
    }

} // extern "C"

namespace wolf::runtime::internal
{

void processBitFieldMonitors()
{
    std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

    for (const auto &monitor : g_BitfieldMonitors)
    {
        if (monitor)
        {
            g_CurrentModId = monitor->modId;
            try
            {
                int result = wolfRuntimeUpdateBitfieldMonitor(reinterpret_cast<WolfBitfieldMonitorHandle>(monitor.get()));
                // if (result > 0)
                // {
                //     ::logDebug("[WOLF] BitField monitor %s detected changes", monitor->description.c_str());
                // }
            }
            catch (...)
            {
                // Continue monitoring other BitField monitors
                ::logError("[WOLF] Exception in BitField monitor %s", monitor->description.c_str());
            }
        }
    }

    g_CurrentModId = 0;
}

} // namespace wolf::runtime::internal