/**
 * @file wolf_shops.hpp
 * @brief WOLF Framework Shop System
 */

#pragma once

#include "wolf_core.hpp"

//==============================================================================
// SHOP SYSTEM
//==============================================================================

namespace wolf
{

namespace detail
{
/**
 * @brief Simple storage for shop callback types
 */
inline std::vector<std::unique_ptr<std::function<void(int, void *)>>> shop_purchase_callbacks;
inline std::vector<std::unique_ptr<std::function<void(int, void *)>>> shop_interact_callbacks;

/**
 * @brief Add a shop purchase callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<void(int, void *)> *addShopPurchaseCallback(std::function<void(int, void *)> &&callback)
{
    auto stored_callback = std::make_unique<std::function<void(int, void *)>>(std::move(callback));
    std::function<void(int, void *)> *callback_ptr = stored_callback.get();
    shop_purchase_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Add a shop interact callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<void(int, void *)> *addShopInteractCallback(std::function<void(int, void *)> &&callback)
{
    auto stored_callback = std::make_unique<std::function<void(int, void *)>>(std::move(callback));
    std::function<void(int, void *)> *callback_ptr = stored_callback.get();
    shop_interact_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Clear all shop callbacks during shutdown
 */
inline void clearShopCallbacks()
{
    shop_purchase_callbacks.clear();
    shop_interact_callbacks.clear();
}

} // namespace detail

/**
 * @brief Shop types for callbacks
 */
enum class ShopType
{
    ItemShop = 0,      /**< Regular item shops */
    DemonFangShop = 1, /**< Demon fang shops */
    SkillShop = 2      /**< Skill shops */
};

/**
 * @brief Add an item to a shop on a specific map
 * @param mapId Map ID where the shop is located (use okami::MapID enum values)
 * @param itemType Type/ID of the item to add
 * @param cost Cost of the item in yen
 * @return True if item was added successfully
 */
inline bool addShopItem(uint32_t mapId, int32_t itemType, int32_t cost)
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->addShopItem(detail::getCurrentModId(), mapId, itemType, cost);
    return true;
}

/**
 * @brief Add an item to a demon fang shop on a specific map
 * @param mapId Map ID where the demon fang shop is located
 * @param itemType Type/ID of the item to add
 * @param cost Cost of the item in demon fangs
 * @return True if item was added successfully
 */
inline bool addDemonFangItem(uint32_t mapId, int32_t itemType, int32_t cost)
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->addDemonFangItem(detail::getCurrentModId(), mapId, itemType, cost);
    return true;
}

/**
 * @brief Set custom sell value for an item type on a specific map
 * @param mapId Map ID where the sell value applies
 * @param itemType Type/ID of the item
 * @param sellValue Custom sell value in yen
 * @return True if sell value was set successfully
 */
inline bool setSellValue(uint32_t mapId, int32_t itemType, int32_t sellValue)
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->setSellValue(detail::getCurrentModId(), mapId, itemType, sellValue);
    return true;
}

/**
 * @brief Remove all shop items added by this mod from a specific map
 * @param mapId Map ID to clean up
 * @return True if cleanup was successful
 */
inline bool removeModShopItems(uint32_t mapId)
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->removeModShopItems(detail::getCurrentModId(), mapId);
    return true;
}

/**
 * @brief Remove all demon fang shop items added by this mod from a specific map
 * @param mapId Map ID to clean up
 * @return True if cleanup was successful
 */
inline bool removeModDemonFangItems(uint32_t mapId)
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->removeModDemonFangItems(detail::getCurrentModId(), mapId);
    return true;
}

/**
 * @brief Remove all shop items added by this mod from all maps
 * @return True if cleanup was successful
 */
inline bool cleanupModShops()
{
    if (!detail::g_runtime)
        return false;

    detail::g_runtime->cleanupModShops(detail::getCurrentModId());
    return true;
}

/**
 * @brief Register callback for shop purchase events (raw access)
 * @param callback Function to call when shop purchases occur, receives raw shop struct pointer
 * @return True if registration succeeded, false otherwise
 */
inline bool onShopPurchase(std::function<void(ShopType shopType, void *shopStruct)> callback)
{
    auto *callback_ptr = detail::addShopPurchaseCallback([callback](int shopType, void *shopStruct) { callback(static_cast<ShopType>(shopType), shopStruct); });

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearShopCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerShopPurchase(
        detail::getCurrentModId(),
        [](int shop_type, void *shop_struct, void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void(int, void *)> *>(userdata);
            try
            {
                (*cb)(shop_type, shop_struct);
            }
            catch (...)
            {
                // Swallow exceptions to prevent crashes
            }
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for shop interaction events (raw access)
 * @param callback Function to call when shop interactions occur, receives raw shop struct pointer
 * @return True if registration succeeded, false otherwise
 */
inline bool onShopInteract(std::function<void(ShopType shopType, void *shopStruct)> callback)
{
    auto *callback_ptr = detail::addShopInteractCallback([callback](int shopType, void *shopStruct) { callback(static_cast<ShopType>(shopType), shopStruct); });

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerShopInteract(
        detail::getCurrentModId(),
        [](int shop_type, void *shop_struct, void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void(int, void *)> *>(userdata);
            try
            {
                (*cb)(shop_type, shop_struct);
            }
            catch (...)
            {
                // Swallow exceptions to prevent crashes
            }
        },
        callback_ptr);
    return true;
}

/**
 * @brief Replace shop icon package with custom icons
 * @param customIconPath Path to custom icon package (e.g., "mymod/CustomShopIcons.dat")
 * @return True if replacement was set successfully
 */
inline bool replaceShopIcons(const char *customIconPath)
{
    if (!detail::g_runtime)
        return false;

    // Intercept the default shop icon package and replace with custom one
    detail::g_runtime->interceptResource(
        detail::getCurrentModId(), "id/ItemShopBuyIcon.dat",
        [](const char *originalPath, void *userdata) -> const char * { return static_cast<const char *>(userdata); }, const_cast<char *>(customIconPath));

    return true;
}

} // namespace wolf