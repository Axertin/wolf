#include "shop_system.h"

#include "../utilities/logger.h"
#include "../utilities/shop_registry.h"
#include "mod_lifecycle.h"

// Global shop callbacks storage
std::mutex g_ShopCallbackMutex;
std::unordered_map<WolfModId, std::unique_ptr<ShopCallbacks>> g_ModShopCallbacks;

// C API implementations
extern "C"
{
    void wolfRuntimeAddShopItem(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost)
    {
        ShopRegistry::instance().addItemToShop(map_id, shop_idx, mod_id, item_type, cost);
    }

    void wolfRuntimeAddDemonFangItem(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost)
    {
        ShopRegistry::instance().addDemonFangItem(map_id, shop_idx, mod_id, item_type, cost);
    }

    void wolfRuntimeSetSellValue(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t sell_value)
    {
        ShopRegistry::instance().setSellValueOverride(map_id, shop_idx, item_type, sell_value);
    }

    void wolfRuntimeRemoveModShopItems(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx)
    {
        ShopRegistry::instance().removeModItemsFromShop(map_id, shop_idx, mod_id);
    }

    void wolfRuntimeRemoveModDemonFangItems(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx)
    {
        ShopRegistry::instance().removeModDemonFangItems(map_id, shop_idx, mod_id);
    }

    void wolfRuntimeCleanupModShops(WolfModId mod_id)
    {
        ShopRegistry::instance().cleanupMod(mod_id);

        // Also cleanup callbacks
        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        g_ModShopCallbacks.erase(mod_id);
    }

    void wolfRuntimeRegisterShopPurchase(WolfModId mod_id, WolfShopPurchaseCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);

        if (g_ModShopCallbacks.find(mod_id) == g_ModShopCallbacks.end())
        {
            g_ModShopCallbacks[mod_id] = std::make_unique<ShopCallbacks>();
        }

        g_ModShopCallbacks[mod_id]->purchaseCallbacks.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterShopInteract(WolfModId mod_id, WolfShopInteractCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);

        if (g_ModShopCallbacks.find(mod_id) == g_ModShopCallbacks.end())
        {
            g_ModShopCallbacks[mod_id] = std::make_unique<ShopCallbacks>();
        }

        g_ModShopCallbacks[mod_id]->interactCallbacks.emplace_back(callback, userdata);
    }

} // extern "C"

namespace wolf::runtime::internal
{

void callShopPurchase(int shopType, void *shopStruct)
{
    std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);

    for (const auto &[modId, callbacks] : g_ModShopCallbacks)
    {
        for (const auto &[callback, userdata] : callbacks->purchaseCallbacks)
        {
            try
            {
                callback(shopType, shopStruct, userdata);
            }
            catch (...)
            {
                logError("[WOLF] Exception in shop purchase callback for mod %u", modId);
            }
        }
    }
}

void callShopInteract(int shopType, void *shopStruct)
{
    std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);

    for (const auto &[modId, callbacks] : g_ModShopCallbacks)
    {
        for (const auto &[callback, userdata] : callbacks->interactCallbacks)
        {
            try
            {
                callback(shopType, shopStruct, userdata);
            }
            catch (...)
            {
                logError("[WOLF] Exception in shop interact callback for mod %u", modId);
            }
        }
    }
}

} // namespace wolf::runtime::internal
