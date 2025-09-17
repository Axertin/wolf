#pragma once

#include <cstdint>

#include "wolf_function_table.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Get the runtime version string
     * @return Version string (e.g., "0.1.0")
     */
    const char *__cdecl wolfRuntimeGetVersion(void);

    /**
     * @brief Get the runtime build information
     * @return Build info string (e.g., "Debug (Clang 20.1.0)")
     */
    const char *__cdecl wolfRuntimeGetBuildInfo(void);

#ifdef __cplusplus
}

//==============================================================================
// C++ INTERNAL API (for runtime implementation)
//==============================================================================

#include <functional>
#include <memory>
#include <string>

// Forward declarations
struct IDXGISwapChain;
typedef unsigned short ImWchar;

namespace wolf::runtime
{

// WolfRuntimeAPI is now defined in wolf_function_table.h

/**
 * @brief Create runtime API function table for mod injection
 * @return Pointer to static function table
 */
::WolfRuntimeAPI *createRuntimeAPI();

/**
 * @brief Process commands that were deferred due to console not being ready
 */
void processPendingCommands();

// Internal functions for runtime implementation
namespace internal
{
// All internal functions have been moved to their respective component headers
} // namespace internal

} // namespace wolf::runtime

#endif
