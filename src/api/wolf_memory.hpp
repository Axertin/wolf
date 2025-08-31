/**
 * @file wolf_memory.hpp
 * @brief WOLF Framework Memory Access System
 */

#pragma once

#include <unordered_map>

#include "wolf_core.hpp"

//==============================================================================
// MEMORY ACCESS
//==============================================================================

namespace wolf
{

namespace detail
{
/**
 * @brief Simple module base address cache for performance
 */
inline std::unordered_map<std::string, uintptr_t> module_cache;

/**
 * @brief Get cached module base address
 * @param module_name Module name to look up
 * @return Module base address or 0 if not found/cached
 */
inline uintptr_t getCachedModuleBase(const char *module_name) noexcept
{
    if (!module_name)
        return 0;

    auto it = module_cache.find(module_name);
    if (it != module_cache.end())
    {
        return it->second;
    }

    // Cache miss - look up via runtime and cache the result
    if (g_runtime)
    {
        uintptr_t base = g_runtime->getModuleBase(module_name);
        module_cache[module_name] = base;
        return base;
    }

    return 0;
}

/**
 * @brief Clear module cache during shutdown
 */
inline void clearModuleCache() noexcept
{
    module_cache.clear();
}
} // namespace detail

/**
 * @brief Get the base address of a loaded module with caching
 * @param moduleName Module name (e.g., "main.dll", "flower_kernel.dll")
 * @return Base address of the module, or 0 if not found
 *
 * Performance Notes:
 * - First access per module hits the runtime API (slow)
 * - Subsequent accesses use cached values (fast)
 */
inline uintptr_t getModuleBase(const char *moduleName) noexcept
{
    if (!moduleName)
        return 0;

    // Register cache cleanup on first use
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearModuleCache(); });
        cleanup_registered = true;
    }

    return detail::getCachedModuleBase(moduleName);
}

/**
 * @brief Check if an address is valid and accessible
 * @param address Address to validate
 * @return True if address is safe to access
 */
inline bool isValidAddress(uintptr_t address) noexcept
{
    if (detail::g_runtime)
    {
        return detail::g_runtime->isValidAddress(address) != 0;
    }
    return false;
}

/**
 * @brief Type-safe memory accessor for direct game memory access
 * @tparam T Type of data at the memory location
 *
 * Provides direct access to game memory with runtime address binding.
 * Optimized for performance, no validation overhead or memory shadows.
 */
template <typename T> class MemoryAccessor
{
    static_assert(std::is_trivially_copyable_v<T>, "MemoryAccessor requires trivially copyable types");

  private:
    uintptr_t address = 0; ///< Memory address being accessed

  public:
    /**
     * @brief Default constructor - creates unbound accessor
     */
    MemoryAccessor() = default;

    /**
     * @brief Construct an accessor for a specific address
     * @param addr Memory address to access
     */
    explicit MemoryAccessor(uintptr_t addr) : address(addr)
    {
    }

    /**
     * @brief Construct an accessor for a specific relative address
     * @param module Module to be relative to
     * @param offset offset from Base
     */
    explicit MemoryAccessor(const char *module, uintptr_t offset) : address(getModuleBase(module) + offset)
    {
    }

    /**
     * @brief Bind to an address (useful for runtime address resolution)
     * @param addr Memory address to access
     */
    void bind(uintptr_t addr)
    {
        address = addr;
    }

    /**
     * @brief Bind to an offset relative to a module
     * @param module Module to be relative to
     * @param offset offset from Base
     */
    void bind(const char *module, uintptr_t offset)
    {
        address = getModuleBase(module) + offset;
    }

    /**
     * @brief Read value directly from memory
     * @return Value at memory location
     * @warning Direct access - no validation or bounds checking performed
     * @warning Not thread-safe - concurrent access may cause data races
     */
    T get() const
    {
        return *reinterpret_cast<volatile T *>(address);
    }

    /**
     * @brief Write value directly to memory
     * @param value Value to write
     * @warning Direct access - no validation or bounds checking performed
     * @warning Not thread-safe - concurrent access may cause data races
     * @warning May crash game if writing to protected memory
     */
    void set(const T &value)
    {
        *reinterpret_cast<volatile T *>(address) = value;
    }

    /**
     * @brief Get raw pointer for direct access
     * @return Pointer to memory location
     * @warning Direct access - no validation performed
     */
    T *get_ptr() const
    {
        return reinterpret_cast<T *>(address);
    }

    /**
     * @brief Get the raw memory address
     * @return Memory address
     */
    uintptr_t raw() const
    {
        return address;
    }

    /**
     * @brief Check if accessor is bound to a valid address
     * @return True if address is non-zero (basic validation only)
     * @note This only checks if the address is non-zero, not if it's valid/accessible
     */
    bool is_bound() const noexcept
    {
        return address != 0;
    }

    /**
     * @brief Compare-and-swap operation (thread-safe atomic operation)
     * @param expected Expected value to compare against
     * @param desired New value to set if comparison succeeds
     * @return True if swap succeeded, false otherwise
     * @note Only available for types that fit in an atomic word size
     */
    template <typename U = T>
    typename std::enable_if_t<sizeof(U) <= sizeof(void *) && std::is_trivially_copyable_v<U>, bool> compare_exchange(T &expected, const T &desired) noexcept
    {
        static_assert(std::is_same_v<U, T>, "Template parameter must match T");
        auto *atomic_ptr = reinterpret_cast<std::atomic<T> *>(address);
        return atomic_ptr->compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
    }

    /**
     * @brief Atomic load operation
     * @return Current value at memory location
     * @note Only available for types that fit in an atomic word size
     */
    template <typename U = T> typename std::enable_if_t<sizeof(U) <= sizeof(void *) && std::is_trivially_copyable_v<U>, T> atomic_load() const noexcept
    {
        static_assert(std::is_same_v<U, T>, "Template parameter must match T");
        auto *atomic_ptr = reinterpret_cast<std::atomic<T> *>(address);
        return atomic_ptr->load(std::memory_order_acquire);
    }

    /**
     * @brief Atomic store operation
     * @param value Value to store
     * @note Only available for types that fit in an atomic word size
     */
    template <typename U = T>
    typename std::enable_if_t<sizeof(U) <= sizeof(void *) && std::is_trivially_copyable_v<U>, void> atomic_store(const T &value) noexcept
    {
        static_assert(std::is_same_v<U, T>, "Template parameter must match T");
        auto *atomic_ptr = reinterpret_cast<std::atomic<T> *>(address);
        atomic_ptr->store(value, std::memory_order_release);
    }

    // ===== CONVENIENCE OPERATORS ====

    /**
     * @brief Implicit conversion to T via get()
     * @return Value at memory location
     */
    operator T() const
    {
        return get();
    }

    /**
     * @brief Assignment operator to write a value
     * @param value Value to write
     * @return Reference to this accessor
     */
    MemoryAccessor &operator=(const T &value)
    {
        set(value);
        return *this;
    }

    /**
     * @brief Access members directly (for struct/class types)
     * @return Pointer to memory location
     */
    T *operator->() const
    {
        return get_ptr();
    }
};

/**
 * @brief Create a typed accessor for module + offset
 * @tparam T Type of data to access
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @return Typed accessor for the memory location
 */
template <typename T> inline MemoryAccessor<T> getMemoryAccessor(const char *module, uintptr_t offset)
{
    return MemoryAccessor<T>(getModuleBase(module) + offset);
}

/**
 * @brief Create a typed accessor for absolute address
 * @tparam T Type of data to access
 * @param absoluteAddress Absolute memory address
 * @return Typed accessor for the memory location
 */
template <typename T> inline MemoryAccessor<T> getMemoryAccessor(uintptr_t absoluteAddress)
{
    return MemoryAccessor<T>(absoluteAddress);
}

/**
 * @brief Read raw bytes from memory
 * @param address Source address
 * @param buffer Destination buffer
 * @param size Number of bytes to read
 * @return True if read succeeded
 */
inline bool readMemory(uintptr_t address, void *buffer, size_t size) noexcept
{
    if (detail::g_runtime && buffer && size > 0)
    {
        return detail::g_runtime->readMemory(address, buffer, size) != 0;
    }
    return false;
}

/**
 * @brief Write raw bytes to memory
 * @param address Destination address
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @return True if write succeeded
 */
inline bool writeMemory(uintptr_t address, const void *buffer, size_t size) noexcept
{
    if (detail::g_runtime && buffer && size > 0)
    {
        return detail::g_runtime->writeMemory(address, buffer, size) != 0;
    }
    return false;
}

/**
 * @brief Search for byte patterns in memory with Optional Caching
 * @param pattern Byte pattern to search for
 * @param mask Pattern mask (? for wildcards, x for exact match)
 * @param module Module to search in (nullptr for all modules)
 * @return Vector of addresses where pattern was found
 *
 * Performance Notes:
 * - Pattern searches are expensive operations
 * - Consider caching results if searching for the same pattern repeatedly
 * - Use getMemoryAccessor() to create accessors from found addresses
 */
inline std::vector<uintptr_t> findPattern(const char *pattern, const char *mask, const char *module = nullptr) noexcept
{
    std::vector<uintptr_t> results;

    struct CallbackData
    {
        std::vector<uintptr_t> *results;
    } data = {&results};

    if (detail::g_runtime && pattern && mask)
    {
        detail::g_runtime->findPattern(
            pattern, mask, module, [](uintptr_t address, void *userdata) noexcept { static_cast<CallbackData *>(userdata)->results->push_back(address); },
            &data);
    }

    return results;
}

/**
 * @brief Find first occurrence of a pattern
 * @param pattern Byte pattern to search for
 * @param mask Pattern mask (? for wildcards, x for exact match)
 * @param module Module to search in (nullptr for all modules)
 * @return Address of first match, or 0 if not found
 *
 * Performance optimized version that stops after finding the first match.
 */
inline uintptr_t findFirstPattern(const char *pattern, const char *mask, const char *module = nullptr) noexcept
{
    uintptr_t result = 0;
    bool found = false;

    struct CallbackData
    {
        uintptr_t *result;
        bool *found;
    } data = {&result, &found};

    if (detail::g_runtime && pattern && mask)
    {
        detail::g_runtime->findPattern(
            pattern, mask, module,
            [](uintptr_t address, void *userdata) noexcept
            {
                auto *cb_data = static_cast<CallbackData *>(userdata);
                if (!*(cb_data->found))
                {
                    *(cb_data->result) = address;
                    *(cb_data->found) = true;
                }
            },
            &data);
    }

    return result;
}

/**
 * @brief Callback function for memory change notifications
 * @param address Address where change occurred
 * @param oldData Previous data at the address
 * @param newData New data at the address
 * @param size Size of the changed data
 */
using MemoryWatchCallback = std::function<void(uintptr_t address, const void *oldData, const void *newData, size_t size)>;

namespace detail
{
/**
 * @brief Simple storage for memory watch callbacks
 */
inline std::vector<std::pair<uintptr_t, std::unique_ptr<MemoryWatchCallback>>> memory_watch_callbacks;

/**
 * @brief Add a memory watch callback
 * @param address Address being watched
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline MemoryWatchCallback *addMemoryWatchCallback(uintptr_t address, MemoryWatchCallback &&callback)
{
    auto stored_callback = std::make_unique<MemoryWatchCallback>(std::move(callback));
    MemoryWatchCallback *callback_ptr = stored_callback.get();
    memory_watch_callbacks.emplace_back(address, std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Remove a memory watch callback
 * @param address Address to stop watching
 * @return True if callback was found and removed
 */
inline bool removeMemoryWatchCallback(uintptr_t address)
{
    auto it = std::find_if(memory_watch_callbacks.begin(), memory_watch_callbacks.end(), [address](const auto &pair) { return pair.first == address; });
    if (it != memory_watch_callbacks.end())
    {
        memory_watch_callbacks.erase(it);
        return true;
    }
    return false;
}

/**
 * @brief Clear all memory watch callbacks
 */
inline void clearMemoryWatchCallbacks()
{
    memory_watch_callbacks.clear();
}
} // namespace detail

/**
 * @brief Watch a memory region for changes
 * @param start Starting address of region
 * @param size Size of region to watch
 * @param callback Function called when changes are detected
 * @param description Optional description for debugging
 * @return True if watch was successfully set up
 */
inline bool watchMemory(uintptr_t start, size_t size, MemoryWatchCallback callback, const char *description = "") noexcept
{
    auto *callback_ptr = detail::addMemoryWatchCallback(start, std::move(callback));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearMemoryWatchCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    bool result = detail::g_runtime->watchMemory(
                      detail::getCurrentModId(), start, size,
                      [](uintptr_t address, const void *old_data, const void *new_data, size_t change_size, void *userdata) noexcept
                      {
                          auto *cb = static_cast<MemoryWatchCallback *>(userdata);
                          (*cb)(address, old_data, new_data, change_size);
                      },
                      callback_ptr, description ? description : "") != 0;

    if (!result)
    {
        // Remove callback if registration failed
        detail::removeMemoryWatchCallback(start);
    }

    return result;
}

/**
 * @brief Stop watching a memory region
 * @param start Starting address of region to unwatch
 * @return True if watch was successfully removed
 */
inline bool unwatchMemory(uintptr_t start) noexcept
{
    // Remove from local storage first
    bool callback_removed = detail::removeMemoryWatchCallback(start);

    if (detail::g_runtime)
    {
        return detail::g_runtime->unwatchMemory(detail::getCurrentModId(), start) != 0;
    }

    return callback_removed; // Return true if we at least cleaned up our callback
}

} // namespace wolf
