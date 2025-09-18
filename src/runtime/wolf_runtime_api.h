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

namespace wolf::runtime
{

/**
 * @brief Create runtime API function table for mod injection
 * @return Pointer to static function table
 */
::WolfRuntimeAPI *createRuntimeAPI();

} // namespace wolf::runtime

#endif
