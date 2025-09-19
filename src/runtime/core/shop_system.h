#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "wolf_types.h"

// Shop callback structures
struct ShopCallbacks
{
    std::vector<std::pair<WolfShopPurchaseCallback, void *>> purchaseCallbacks;
    std::vector<std::pair<WolfShopInteractCallback, void *>> interactCallbacks;
};

// Global shop callbacks storage (defined in shop_system.cpp)
extern std::mutex g_ShopCallbackMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ShopCallbacks>> g_ModShopCallbacks;

// Internal functions for shop system (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
void callShopPurchase(int shopType, void *shopStruct);
void callShopInteract(int shopType, void *shopStruct);
} // namespace wolf::runtime::internal

// C API functions for shop system
extern "C"
{
    void __cdecl wolfRuntimeAddShopItem(WolfModId mod_id, uint32_t map_id, int32_t item_type, int32_t cost);
    void __cdecl wolfRuntimeAddDemonFangItem(WolfModId mod_id, uint32_t map_id, int32_t item_type, int32_t cost);
    void __cdecl wolfRuntimeSetSellValue(WolfModId mod_id, uint32_t map_id, int32_t item_type, int32_t sell_value);
    void __cdecl wolfRuntimeRemoveModShopItems(WolfModId mod_id, uint32_t map_id);
    void __cdecl wolfRuntimeRemoveModDemonFangItems(WolfModId mod_id, uint32_t map_id);
    void __cdecl wolfRuntimeCleanupModShops(WolfModId mod_id);

    void __cdecl wolfRuntimeRegisterShopPurchase(WolfModId mod_id, WolfShopPurchaseCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterShopInteract(WolfModId mod_id, WolfShopInteractCallback callback, void *userdata);
}