#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wolf_types.h"

// Resource interception structure
struct ResourceIntercept
{
    WolfModId modId;
    std::string pattern;
    bool isPattern; // true if wildcard pattern, false if exact filename
    WolfResourceProvider provider;
    void *userdata;
};

// Global resource system storage (defined in resource_system.cpp)
extern std::mutex g_ResourceMutex;
extern std::vector<std::unique_ptr<ResourceIntercept>> g_ResourceIntercepts;

inline bool matchesPattern(const std::string &filename, const std::string &pattern);

// Internal functions for resource system (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
const char *interceptResourceLoad(const char *originalPath);
} // namespace wolf::runtime::internal

// C API functions for resource system
extern "C"
{
    void __cdecl wolfRuntimeInterceptResource(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata);
    void __cdecl wolfRuntimeRemoveResourceInterception(WolfModId mod_id, const char *filename);
    void __cdecl wolfRuntimeInterceptResourcePattern(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata);
}
