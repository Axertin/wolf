/**
 * @file wolf_resources.hpp
 * @brief WOLF Framework Resource System
 */

#pragma once

#include "wolf_core.hpp"

//==============================================================================
// RESOURCE SYSTEM
//==============================================================================

namespace wolf
{

/**
 * @brief Function signature for resource providers
 * @param originalPath The original resource path requested by game
 * @return Path to replacement resource, or nullptr to use original
 */
using ResourceProvider = std::function<const char *(const char *originalPath)>;

namespace detail
{
/**
 * @brief Simple storage for resource provider callbacks
 */
inline std::vector<std::unique_ptr<ResourceProvider>> resource_provider_callbacks;

/**
 * @brief Add a resource provider callback
 * @param provider Provider to store
 * @return Pointer to stored provider for runtime registration
 */
inline ResourceProvider *addResourceProvider(ResourceProvider &&provider)
{
    auto stored_provider = std::make_unique<ResourceProvider>(std::move(provider));
    ResourceProvider *provider_ptr = stored_provider.get();
    resource_provider_callbacks.push_back(std::move(stored_provider));
    return provider_ptr;
}

/**
 * @brief Clear all resource provider callbacks during shutdown
 */
inline void clearResourceProviderCallbacks()
{
    resource_provider_callbacks.clear();
}

} // namespace detail

/**
 * @brief Intercept loading of a specific resource file
 * @param filename Exact filename to intercept (e.g., "id/ItemShopBuyIcon.dat")
 * @param provider Function that returns replacement path
 * @return True if interception was successfully set up
 */
inline bool interceptResource(const char *filename, ResourceProvider provider) noexcept
{
    ResourceProvider *provider_ptr = detail::addResourceProvider(std::move(provider));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearResourceProviderCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->interceptResource(
        detail::getCurrentModId(), filename,
        [](const char *original_path, void *userdata) noexcept -> const char *
        {
            auto *prov = static_cast<ResourceProvider *>(userdata);
            return (*prov)(original_path);
        },
        provider_ptr);
    return true;
}

/**
 * @brief Remove resource interception
 * @param filename Filename to stop intercepting
 * @return True if interception was successfully removed
 */
inline bool removeResourceInterception(const char *filename) noexcept
{
    if (!detail::g_runtime)
        return false;
    detail::g_runtime->removeResourceInterception(detail::getCurrentModId(), filename);
    return true;
}

/**
 * @brief Intercept resources matching a pattern
 * @param pattern Wildcard pattern (e.g., "*.dat", "textures/...")
 * @param provider Function that returns replacement path
 * @return True if pattern interception was successfully set up
 */
inline bool interceptResourcePattern(const char *pattern, ResourceProvider provider) noexcept
{
    ResourceProvider *provider_ptr = detail::addResourceProvider(std::move(provider));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->interceptResourcePattern(
        detail::getCurrentModId(), pattern,
        [](const char *original_path, void *userdata) noexcept -> const char *
        {
            auto *prov = static_cast<ResourceProvider *>(userdata);
            return (*prov)(original_path);
        },
        provider_ptr);
    return true;
}

} // namespace wolf
