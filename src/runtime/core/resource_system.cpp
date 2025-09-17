#include "resource_system.h"

#include <algorithm>

// Global resource system storage
std::mutex g_ResourceMutex;
std::vector<std::unique_ptr<ResourceIntercept>> g_ResourceIntercepts;

inline bool matchesPattern(const std::string &filename, const std::string &pattern)
{
    // Simple wildcard matching - supports * and ?
    // This is a basic implementation - could be enhanced
    if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos)
        return filename == pattern; // Exact match

    // TODO: Implement proper wildcard matching if needed
    return filename.find(pattern.substr(0, pattern.find('*'))) != std::string::npos;
}

// C API implementations
extern "C"
{
    void wolfRuntimeInterceptResource(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata)
    {
        if (!filename || !provider)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        auto intercept = std::make_unique<ResourceIntercept>();
        intercept->modId = mod_id;
        intercept->pattern = filename;
        intercept->isPattern = false; // Exact filename match
        intercept->provider = provider;
        intercept->userdata = userdata;

        g_ResourceIntercepts.push_back(std::move(intercept));
    }

    void wolfRuntimeRemoveResourceInterception(WolfModId mod_id, const char *filename)
    {
        if (!filename)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        std::string target(filename);
        auto it =
            std::remove_if(g_ResourceIntercepts.begin(), g_ResourceIntercepts.end(), [mod_id, &target](const std::unique_ptr<ResourceIntercept> &intercept)
                           { return intercept->modId == mod_id && intercept->pattern == target && !intercept->isPattern; });

        if (it != g_ResourceIntercepts.end())
        {
            g_ResourceIntercepts.erase(it, g_ResourceIntercepts.end());
        }
    }

    void wolfRuntimeInterceptResourcePattern(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata)
    {
        if (!pattern || !provider)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        auto intercept = std::make_unique<ResourceIntercept>();
        intercept->modId = mod_id;
        intercept->pattern = pattern;
        intercept->isPattern = true; // Wildcard pattern match
        intercept->provider = provider;
        intercept->userdata = userdata;

        g_ResourceIntercepts.push_back(std::move(intercept));
    }

} // extern "C"

namespace wolf::runtime::internal
{

const char *interceptResourceLoad(const char *originalPath)
{
    if (!originalPath)
        return nullptr;

    std::lock_guard<std::mutex> lock(g_ResourceMutex);

    std::string path(originalPath);

    // Check exact filename matches first
    for (const auto &intercept : g_ResourceIntercepts)
    {
        if (!intercept->isPattern && intercept->pattern == path)
        {
            const char *replacement = intercept->provider(originalPath, intercept->userdata);
            if (replacement)
            {
                return replacement;
            }
        }
    }

    // Then check pattern matches
    for (const auto &intercept : g_ResourceIntercepts)
    {
        if (intercept->isPattern && matchesPattern(path, intercept->pattern))
        {
            const char *replacement = intercept->provider(originalPath, intercept->userdata);
            if (replacement)
            {
                return replacement;
            }
        }
    }

    return nullptr; // No interception
}

} // namespace wolf::runtime::internal
