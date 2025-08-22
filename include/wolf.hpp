/**
 * @file wolf.hpp
 * @brief WOLF Okami Loader Framework - Mod Development API
 *
 * The wolf framework provides a header-only C++ interface for creating mods for Okami HD.
 * All functionality is implemented inline and calls into the runtime C API.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declare ImGui types to avoid hard dependency
#ifndef IMGUI_VERSION
struct ImVec4
{
    float x, y, z, w;
    constexpr ImVec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f)
    {
    }
    constexpr ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w)
    {
    }
};
#endif

//==============================================================================
// C RUNTIME API DECLARATIONS
// These function pointers are provided by the runtime via function table injection
//==============================================================================

extern "C"
{
    //--- MOD IDENTIFICATION & LIFECYCLE ---

    /**
     * @brief Opaque handle representing a loaded mod
     */
    typedef int WolfModId;

    /**
     * @brief Mod interface structure (ABI-stable)
     */
    typedef void(__cdecl *WolfModInitFunc)(void);
    typedef void(__cdecl *WolfModShutdownFunc)(void);
    typedef const char *(__cdecl *WolfModStringFunc)(void);

    typedef struct WolfModInterface
    {
        WolfModInitFunc earlyGameInit; ///< Early initialization callback (can be NULL)
        WolfModInitFunc lateGameInit;  ///< Late initialization callback (can be NULL)
        WolfModShutdownFunc shutdown;  ///< Shutdown callback (required)
        WolfModStringFunc getName;     ///< Get mod name (required)
        WolfModStringFunc getVersion;  ///< Get mod version (can be NULL for default)
    } WolfModInterface;

    //--- LOGGING ---

    /**
     * @brief Log levels
     */
    typedef enum WolfLogLevel
    {
        WOLF_LOG_INFO = 0,
        WOLF_LOG_WARNING = 1,
        WOLF_LOG_ERROR = 2,
        WOLF_LOG_DEBUG = 3
    } WolfLogLevel;

    //--- CALLBACK TYPES ---

    /**
     * @brief Pattern search callback
     */
    typedef void(__cdecl *WolfPatternCallback)(uintptr_t address, void *userdata);

    /**
     * @brief Memory change callback
     */
    typedef void(__cdecl *WolfMemoryWatchCallback)(uintptr_t address, const void *old_data, const void *new_data, size_t size, void *userdata);

    /**
     * @brief Game event callback function type
     */
    typedef void(__cdecl *WolfGameEventCallback)(void *userdata);

    /**
     * @brief Item pickup callback function type
     */
    typedef void(__cdecl *WolfItemPickupCallback)(int item_id, int count, void *userdata);

    /**
     * @brief Console command callback
     */
    typedef void(__cdecl *WolfConsoleCommandCallback)(int argc, const char **argv, void *userdata);

    /**
     * @brief Resource provider callback
     */
    typedef const char *(__cdecl *WolfResourceProvider)(const char *original_path, void *userdata);

    //--- RUNTIME API FUNCTION TABLE ---

    /**
     * @brief Function table provided by runtime to mods
     * All runtime functionality is accessed through this table
     */
    typedef struct WolfRuntimeAPI
    {
        // Mod lifecycle
        WolfModId(__cdecl *getCurrentModId)(void);
        WolfModId(__cdecl *registerMod)(const WolfModInterface *modInterface);

        // Logging
        void(__cdecl *log)(WolfModId mod_id, WolfLogLevel level, const char *message);
        void(__cdecl *setLogPrefix)(WolfModId mod_id, const char *prefix);

        // Memory access
        uintptr_t(__cdecl *getModuleBase)(const char *module_name);
        int(__cdecl *isValidAddress)(uintptr_t address);
        int(__cdecl *readMemory)(uintptr_t address, void *buffer, size_t size);
        int(__cdecl *writeMemory)(uintptr_t address, const void *buffer, size_t size);
        void(__cdecl *findPattern)(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata);
        int(__cdecl *watchMemory)(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description);
        int(__cdecl *unwatchMemory)(WolfModId mod_id, uintptr_t start);

        // Game hooks & callbacks
        void(__cdecl *registerGameTick)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
        void(__cdecl *registerGameStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
        void(__cdecl *registerGameStop)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
        void(__cdecl *registerPlayStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
        void(__cdecl *registerReturnToMenu)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
        void(__cdecl *registerItemPickup)(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata);
        int(__cdecl *hookFunction)(uintptr_t address, void *detour, void **original);

        // Console system
        void(__cdecl *addCommand)(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description);
        void(__cdecl *removeCommand)(WolfModId mod_id, const char *name);
        void(__cdecl *executeCommand)(const char *command_line);
        void(__cdecl *consolePrint)(const char *message);
        int(__cdecl *isConsoleVisible)(void);

        // Resource system
        void(__cdecl *interceptResource)(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata);
        void(__cdecl *removeResourceInterception)(WolfModId mod_id, const char *filename);
        void(__cdecl *interceptResourcePattern)(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata);
    } WolfRuntimeAPI;

} // extern "C"

//==============================================================================
// C++ FRAMEWORK IMPLEMENTATION
//==============================================================================

namespace wolf
{

//==============================================================================
// VERSION INFO
//==============================================================================

/**
 * @brief Get framework version
 * @return Version string
 */
inline const char *getVersion()
{
    return "0.1.0";
}

/**
 * @brief Get build information
 * @return Build info string
 */
inline const char *getBuildInfo()
{
    return "WOLF API v0.1.0 - Debug (Clang 20.1.0)";
}

//==============================================================================
// MOD INTERFACE & REGISTRATION
//==============================================================================

namespace detail
{
// Global runtime API pointer - set during mod initialization
inline WolfRuntimeAPI *g_runtime = nullptr;

// Thread-local mod ID storage for automatic mod identification
inline WolfModId &getCurrentModIdRef()
{
    static thread_local WolfModId mod_id = -1;
    return mod_id;
}

// Automatic mod ID retrieval
inline WolfModId getCurrentModId()
{
    auto &mod_id = getCurrentModIdRef();
    if (mod_id == -1 && g_runtime)
    {
        mod_id = g_runtime->getCurrentModId();
    }
    return mod_id;
}
} // namespace detail

/**
 * @brief Register a C-style mod interface with the WOLF framework (recommended)
 * @param earlyInit Function for early initialization (or nullptr)
 * @param lateInit Function for late initialization (or nullptr)
 * @param shutdownFunc Function for cleanup (required)
 * @param nameFunc Function returning mod name (required)
 * @param versionFunc Function returning version (or nullptr for default)
 *
 * Usage:
 * @code
 * void myEarlyInit() { ... }
 * void myLateInit() { ... }
 * void myShutdown() { ... }
 * const char* myGetName() { return "MyMod"; }
 * const char* myGetVersion() { return "1.0.0"; }
 *
 * WOLF_MOD_ENTRY(myEarlyInit, myLateInit, myShutdown, myGetName, myGetVersion)
 * @endcode
 */
#define WOLF_MOD_ENTRY(earlyInit, lateInit, shutdownFunc, nameFunc, versionFunc)                                                                               \
    extern "C" __declspec(dllexport) WolfModInterface __cdecl wolfGetModInterface(WolfRuntimeAPI *runtime)                                                     \
    {                                                                                                                                                          \
        wolf::detail::g_runtime = runtime;                                                                                                                     \
        WolfModInterface interface = {(earlyInit), (lateInit), (shutdownFunc), (nameFunc), (versionFunc)};                                                     \
        wolf::detail::getCurrentModIdRef() = runtime->registerMod(&interface);                                                                                 \
        return interface;                                                                                                                                      \
    }

/**
 * @brief Simplified macro for common mod pattern
 * @param ModClass A class with static methods: earlyGameInit(), lateGameInit(), shutdown(), getName(), getVersion()
 *
 * Usage:
 * @code
 * class MyMod {
 * public:
 *     static void earlyGameInit() { ... }
 *     static void lateGameInit() { ... }
 *     static void shutdown() { ... }
 *     static const char* getName() { return "MyMod"; }
 *     static const char* getVersion() { return "1.0.0"; }
 * };
 * WOLF_MOD_ENTRY_CLASS(MyMod)
 * @endcode
 */
#define WOLF_MOD_ENTRY_CLASS(ModClass)                                                                                                                         \
    extern "C" __declspec(dllexport) WolfModInterface __cdecl wolfGetModInterface(WolfRuntimeAPI *runtime)                                                     \
    {                                                                                                                                                          \
        wolf::detail::g_runtime = runtime;                                                                                                                     \
        WolfModInterface interface = {ModClass::earlyGameInit, ModClass::lateGameInit, ModClass::shutdown, ModClass::getName, ModClass::getVersion};           \
        wolf::detail::getCurrentModIdRef() = runtime->registerMod(&interface);                                                                                 \
        return interface;                                                                                                                                      \
    }

//==============================================================================
// LOGGING
//==============================================================================

/**
 * @brief Log message severity levels
 */
enum class LogLevel
{
    Info = 0,    ///< General information
    Warning = 1, ///< Warning conditions
    Error = 2,   ///< Error conditions
    Debug = 3    ///< Debug information
};

namespace detail
{
// Convert C++ log level to C enum
inline WolfLogLevel toCLogLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return WOLF_LOG_INFO;
    case LogLevel::Warning:
        return WOLF_LOG_WARNING;
    case LogLevel::Error:
        return WOLF_LOG_ERROR;
    case LogLevel::Debug:
        return WOLF_LOG_DEBUG;
    default:
        return WOLF_LOG_INFO;
    }
}

// Helper for printf-style formatting
inline std::string formatString(const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (size <= 0)
        return std::string(format);

    std::string result(size, '\0');
    vsnprintf(&result[0], size + 1, format, args);
    return result;
}

// Printf-style formatting wrapper
inline std::string formatString(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string result = formatString(format, args);
    va_end(args);
    return result;
}
} // namespace detail

/**
 * @brief Log an info message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logInfo(const char *format, ...)
{
    if (!detail::g_runtime)
        return;
    va_list args;
    va_start(args, format);
    std::string message = detail::formatString(format, args);
    va_end(args);
    detail::g_runtime->log(detail::getCurrentModId(), WOLF_LOG_INFO, message.c_str());
}

/**
 * @brief Log a warning message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logWarning(const char *format, ...)
{
    if (!detail::g_runtime)
        return;
    va_list args;
    va_start(args, format);
    std::string message = detail::formatString(format, args);
    va_end(args);
    detail::g_runtime->log(detail::getCurrentModId(), WOLF_LOG_WARNING, message.c_str());
}

/**
 * @brief Log an error message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logError(const char *format, ...)
{
    if (!detail::g_runtime)
        return;
    va_list args;
    va_start(args, format);
    std::string message = detail::formatString(format, args);
    va_end(args);
    detail::g_runtime->log(detail::getCurrentModId(), WOLF_LOG_ERROR, message.c_str());
}

/**
 * @brief Log a debug message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logDebug(const char *format, ...)
{
    if (!detail::g_runtime)
        return;
    va_list args;
    va_start(args, format);
    std::string message = detail::formatString(format, args);
    va_end(args);
    detail::g_runtime->log(detail::getCurrentModId(), WOLF_LOG_DEBUG, message.c_str());
}

/**
 * @brief Set a prefix for all log messages from this mod
 * @param prefix Prefix string (e.g., "[MyMod]")
 */
inline void setLogPrefix(const char *prefix)
{
    if (!detail::g_runtime)
        return;
    detail::g_runtime->setLogPrefix(detail::getCurrentModId(), prefix);
}

//==============================================================================
// MEMORY ACCESS
//==============================================================================

/**
 * @brief Get the base address of a loaded module
 * @param moduleName Module name (e.g., "main.dll", "flower_kernel.dll")
 * @return Base address of the module, or 0 if not found
 */
inline uintptr_t getModuleBase(const char *moduleName)
{
    return detail::g_runtime ? detail::g_runtime->getModuleBase(moduleName) : 0;
}

/**
 * @brief Check if an address is valid and accessible
 * @param address Address to validate
 * @return True if address is safe to access
 */
inline bool isValidAddress(uintptr_t address)
{
    return detail::g_runtime && detail::g_runtime->isValidAddress(address);
}

/**
 * @brief Type-safe memory accessor for direct game memory access
 * @tparam T Type of data at the memory location
 *
 * Provides direct access to game memory with runtime address binding.
 * Optimized for performance - no validation overhead or memory shadows.
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
     * @warning Direct access - no validation performed
     */
    T get() const
    {
        return *reinterpret_cast<volatile T *>(address);
    }

    /**
     * @brief Write value directly to memory
     * @param value Value to write
     * @warning Direct access - no validation performed
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
inline bool readMemory(uintptr_t address, void *buffer, size_t size)
{
    return detail::g_runtime && detail::g_runtime->readMemory(address, buffer, size);
}

/**
 * @brief Write raw bytes to memory
 * @param address Destination address
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @return True if write succeeded
 */
inline bool writeMemory(uintptr_t address, const void *buffer, size_t size)
{
    return detail::g_runtime && detail::g_runtime->writeMemory(address, buffer, size);
}

/**
 * @brief Search for byte patterns in memory
 * @param pattern Byte pattern to search for
 * @param mask Pattern mask (? for wildcards, x for exact match)
 * @param module Module to search in (nullptr for all modules)
 * @return Vector of addresses where pattern was found
 */
inline std::vector<uintptr_t> findPattern(const char *pattern, const char *mask, const char *module = nullptr)
{
    std::vector<uintptr_t> results;

    struct CallbackData
    {
        std::vector<uintptr_t> *results;
    } data = {&results};

    if (!detail::g_runtime)
        return results;
    detail::g_runtime->findPattern(
        pattern, mask, module, [](uintptr_t address, void *userdata) { static_cast<CallbackData *>(userdata)->results->push_back(address); }, &data);

    return results;
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
// Storage for memory watch callbacks (in mod's address space)
inline std::vector<std::pair<uintptr_t, std::unique_ptr<MemoryWatchCallback>>> &getMemoryWatchCallbacks()
{
    static std::vector<std::pair<uintptr_t, std::unique_ptr<MemoryWatchCallback>>> callbacks;
    return callbacks;
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
inline bool watchMemory(uintptr_t start, size_t size, MemoryWatchCallback callback, const char *description = "")
{
    auto &callbacks = detail::getMemoryWatchCallbacks();
    auto stored_callback = std::make_unique<MemoryWatchCallback>(std::move(callback));
    MemoryWatchCallback *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return false;
    bool result = detail::g_runtime->watchMemory(
                      detail::getCurrentModId(), start, size,
                      [](uintptr_t address, const void *old_data, const void *new_data, size_t size, void *userdata)
                      {
                          auto *cb = static_cast<MemoryWatchCallback *>(userdata);
                          (*cb)(address, old_data, new_data, size);
                      },
                      callback_ptr, description) != 0;

    if (result)
    {
        callbacks.emplace_back(start, std::move(stored_callback));
    }

    return result;
}

/**
 * @brief Stop watching a memory region
 * @param start Starting address of region to unwatch
 * @return True if watch was successfully removed
 */
inline bool unwatchMemory(uintptr_t start)
{
    auto &callbacks = detail::getMemoryWatchCallbacks();

    // Remove from local storage
    callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(), [start](const auto &pair) { return pair.first == start; }), callbacks.end());

    return detail::g_runtime && detail::g_runtime->unwatchMemory(detail::getCurrentModId(), start);
}

//==============================================================================
// GAME HOOKS & CALLBACKS
//==============================================================================

namespace detail
{
// Storage for game event callbacks (in mod's address space)
inline std::vector<std::unique_ptr<std::function<void()>>> &getGameEventCallbacks()
{
    static std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    return callbacks;
}

inline std::vector<std::unique_ptr<std::function<void(int, int)>>> &getItemPickupCallbacks()
{
    static std::vector<std::unique_ptr<std::function<void(int, int)>>> callbacks;
    return callbacks;
}
} // namespace detail

/**
 * @brief Register callback for game tick (called every frame)
 * @param callback Function to call each frame
 */
inline void onGameTick(std::function<void()> callback)
{
    auto &callbacks = detail::getGameEventCallbacks();
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerGameTick(
        detail::getCurrentModId(),
        [](void *userdata)
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Register callback for game start
 * @param callback Function to call when game starts
 */
inline void onGameStart(std::function<void()> callback)
{
    auto &callbacks = detail::getGameEventCallbacks();
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerGameStart(
        detail::getCurrentModId(),
        [](void *userdata)
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Register callback for game stop
 * @param callback Function to call when game stops
 */
inline void onGameStop(std::function<void()> callback)
{
    auto &callbacks = detail::getGameEventCallbacks();
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerGameStop(
        detail::getCurrentModId(),
        [](void *userdata)
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Register callback for gameplay start (i.e, not in main menu and after a save file loads)
 * @param callback Function to call when gameplay starts
 */
inline void onPlayStart(std::function<void()> callback)
{
    auto &callbacks = detail::getGameEventCallbacks();
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerPlayStart(
        detail::getCurrentModId(),
        [](void *userdata)
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Register callback for return to menu
 * @param callback Function to call when returning to menu
 */
inline void onReturnToMenu(std::function<void()> callback)
{
    auto &callbacks = detail::getGameEventCallbacks();
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerReturnToMenu(
        detail::getCurrentModId(),
        [](void *userdata)
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Register callback for item pickup events
 * @param callback Function to call when player picks up items
 */
inline void onItemPickup(std::function<void(int itemId, int count)> callback)
{
    auto &callbacks = detail::getItemPickupCallbacks();
    auto stored_callback = std::make_unique<std::function<void(int, int)>>(std::move(callback));
    std::function<void(int, int)> *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->registerItemPickup(
        detail::getCurrentModId(),
        [](int item_id, int count, void *userdata)
        {
            auto *cb = static_cast<std::function<void(int, int)> *>(userdata);
            (*cb)(item_id, count);
        },
        callback_ptr);

    callbacks.push_back(std::move(stored_callback));
}

/**
 * @brief Hook a function at absolute address
 * @tparam FuncSig Function signature type
 * @param address Absolute memory address of function
 * @param detour Replacement function
 * @param original Pointer to store original function (optional)
 * @return True if hook was successfully created
 */
template <typename FuncSig> inline bool hookFunction(uintptr_t address, FuncSig detour, FuncSig *original = nullptr)
{
    void **orig_ptr = original ? reinterpret_cast<void **>(original) : nullptr;
    return detail::g_runtime && detail::g_runtime->hookFunction(address, reinterpret_cast<void *>(detour), orig_ptr);
}

/**
 * @brief Hook a function at module + offset
 * @tparam FuncSig Function signature type
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @param detour Replacement function
 * @param original Pointer to store original function (optional)
 * @return True if hook was successfully created
 */
template <typename FuncSig> inline bool hookFunction(const char *module, uintptr_t offset, FuncSig detour, FuncSig *original = nullptr)
{
    return hookFunction(getModuleBase(module) + offset, detour, original);
}

/**
 * @brief Replace a function completely (no original call)
 * @tparam FuncSig Function signature type
 * @param address Absolute memory address of function
 * @param replacement Replacement function
 * @return True if replacement was successful
 */
template <typename FuncSig> inline bool replaceFunction(uintptr_t address, FuncSig replacement)
{
    return hookFunction(address, replacement, nullptr);
}

/**
 * @brief Replace a function at module + offset
 * @tparam FuncSig Function signature type
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @param replacement Replacement function
 * @return True if replacement was successful
 */
template <typename FuncSig> inline bool replaceFunction(const char *module, uintptr_t offset, FuncSig replacement)
{
    return replaceFunction(getModuleBase(module) + offset, replacement);
}

//==============================================================================
// CONSOLE SYSTEM
//==============================================================================

/**
 * @brief Function signature for console command handlers
 * @param args Vector of command arguments (including command name at [0])
 */
using CommandHandler = std::function<void(const std::vector<std::string> &args)>;

namespace detail
{
// Storage for console command callbacks (in mod's address space)
inline std::vector<std::unique_ptr<CommandHandler>> &getCommandCallbacks()
{
    static std::vector<std::unique_ptr<CommandHandler>> callbacks;
    return callbacks;
}
} // namespace detail

/**
 * @brief Register a console command
 * @param name Command name (case-sensitive)
 * @param handler Function to call when command is executed
 * @param description Help text for the command (optional)
 */
namespace detail
{
// C-style callback wrapper for commands
inline void __cdecl commandCallbackWrapper(int argc, const char **argv, void *userdata)
{
    auto *cb = static_cast<CommandHandler *>(userdata);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    (*cb)(args);
}
} // namespace detail

inline void addCommand(const char *name, CommandHandler handler, const char *description = "")
{
    auto &callbacks = detail::getCommandCallbacks();
    auto stored_callback = std::make_unique<CommandHandler>(std::move(handler));
    CommandHandler *callback_ptr = stored_callback.get();

    if (!detail::g_runtime)
    {
        logError("addCommand: Runtime not available!");
        return;
    }

    logInfo("Registering command: %s", name);
    detail::g_runtime->addCommand(detail::getCurrentModId(), name, detail::commandCallbackWrapper, callback_ptr, description);

    callbacks.push_back(std::move(stored_callback));
    logInfo("Command registered successfully: %s", name);
}

/**
 * @brief Unregister a console command
 * @param name Command name to remove
 */
inline void removeCommand(const char *name)
{
    if (!detail::g_runtime)
        return;
    detail::g_runtime->removeCommand(detail::getCurrentModId(), name);
}

/**
 * @brief Execute a command line string
 * @param commandLine Full command with arguments
 */
inline void executeCommand(const std::string &commandLine)
{
    if (!detail::g_runtime)
        return;
    detail::g_runtime->executeCommand(commandLine.c_str());
}

/**
 * @brief Print a message to the console
 * @param message Message to display
 */
inline void consolePrint(const char *message)
{
    if (!detail::g_runtime)
        return;
    detail::g_runtime->consolePrint(message);
}

/**
 * @brief Print a formatted message to the console
 * @param format Printf-style format string
 * @param ... Format arguments
 */
inline void consolePrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = detail::formatString(format, args);
    va_end(args);
    if (!detail::g_runtime)
        return;
    detail::g_runtime->consolePrint(message.c_str());
}

/**
 * @brief Check if console window is visible
 * @return True if console is visible
 */
inline bool isConsoleVisible()
{
    return detail::g_runtime && detail::g_runtime->isConsoleVisible();
}

//==============================================================================
// RESOURCE SYSTEM
//==============================================================================

/**
 * @brief Function signature for resource providers
 * @param originalPath The original resource path requested by game
 * @return Path to replacement resource, or nullptr to use original
 */
using ResourceProvider = std::function<const char *(const char *originalPath)>;

namespace detail
{
// Storage for resource provider callbacks (in mod's address space)
inline std::vector<std::unique_ptr<ResourceProvider>> &getResourceProviders()
{
    static std::vector<std::unique_ptr<ResourceProvider>> providers;
    return providers;
}
} // namespace detail

/**
 * @brief Intercept loading of a specific resource file
 * @param filename Exact filename to intercept (e.g., "id/ItemShopBuyIcon.dat")
 * @param provider Function that returns replacement path
 */
inline void interceptResource(const char *filename, ResourceProvider provider)
{
    auto &providers = detail::getResourceProviders();
    auto stored_provider = std::make_unique<ResourceProvider>(std::move(provider));
    ResourceProvider *provider_ptr = stored_provider.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->interceptResource(
        detail::getCurrentModId(), filename,
        [](const char *original_path, void *userdata) -> const char *
        {
            auto *prov = static_cast<ResourceProvider *>(userdata);
            return (*prov)(original_path);
        },
        provider_ptr);

    providers.push_back(std::move(stored_provider));
}

/**
 * @brief Remove resource interception
 * @param filename Filename to stop intercepting
 */
inline void removeResourceInterception(const char *filename)
{
    if (!detail::g_runtime)
        return;
    detail::g_runtime->removeResourceInterception(detail::getCurrentModId(), filename);
}

/**
 * @brief Intercept resources matching a pattern
 * @param pattern Wildcard pattern (e.g., "*.dat", "textures/...")
 * @param provider Function that returns replacement path
 */
inline void interceptResourcePattern(const char *pattern, ResourceProvider provider)
{
    auto &providers = detail::getResourceProviders();
    auto stored_provider = std::make_unique<ResourceProvider>(std::move(provider));
    ResourceProvider *provider_ptr = stored_provider.get();

    if (!detail::g_runtime)
        return;
    detail::g_runtime->interceptResourcePattern(
        detail::getCurrentModId(), pattern,
        [](const char *original_path, void *userdata) -> const char *
        {
            auto *prov = static_cast<ResourceProvider *>(userdata);
            return (*prov)(original_path);
        },
        provider_ptr);

    providers.push_back(std::move(stored_provider));
}

} // namespace wolf
