#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "okami/shopdata.h"
#include "wolf_types.h"

/**
 * @brief Shop item definition for mod registration
 */
struct ShopItem
{
    int32_t itemType;
    int32_t cost;
    WolfModId modId;
};

/**
 * @brief Shop definition containing all items from all mods
 */
class ShopDefinition
{
  private:
    std::vector<uint8_t> dataISL;
    bool dirty = true;
    okami::SellValueArray sellValues = okami::DefaultItemSellPrices;
    std::vector<ShopItem> items;
    mutable std::mutex itemsMutex;

    void rebuildISL();
    void checkDirty();

  public:
    ShopDefinition();

    // Warning: Do NOT call when shop is currently open.
    const uint8_t *getData();

    void addItem(WolfModId modId, int32_t itemType, int32_t cost);
    void removeModItems(WolfModId modId);
    void clearAllItems();
    void setSellValueOverride(int32_t itemType, int32_t sellValue);
    void setSellValues(const okami::SellValueArray &replacementSellValues);

    // Get items for specific mod (for debugging/inspection)
    std::vector<ShopItem> getModItems(WolfModId modId) const;
};

// Demon fang shops store ItemShopStock directly (no mod tracking needed)

/**
 * @brief Global shop registry that manages all shops across all mods
 */
class ShopRegistry
{
  private:
    std::mutex registryMutex;
    std::unordered_map<uint64_t, std::unique_ptr<ShopDefinition>> itemShops;        // (mapId << 32 | shopIdx) -> shop
    std::unordered_map<uint64_t, std::vector<okami::ItemShopStock>> demonFangShops; // (mapId << 32 | shopIdx) -> items

    static uint64_t makeShopKey(uint32_t mapId, uint32_t shopIdx)
    {
        return (static_cast<uint64_t>(mapId) << 32) | shopIdx;
    }

  public:
    static ShopRegistry &instance();

    // Item shop management
    void addItemToShop(uint32_t mapId, uint32_t shopIdx, WolfModId modId, int32_t itemType, int32_t cost);
    void removeModItemsFromShop(uint32_t mapId, uint32_t shopIdx, WolfModId modId);
    void setSellValueOverride(uint32_t mapId, uint32_t shopIdx, int32_t itemType, int32_t sellValue);
    const uint8_t *getShopData(uint32_t mapId, uint32_t shopIdx);

    // Demon fang shop management
    void addDemonFangItem(uint32_t mapId, uint32_t shopIdx, WolfModId modId, int32_t itemType, int32_t cost);
    void removeModDemonFangItems(uint32_t mapId, uint32_t shopIdx, WolfModId modId);
    okami::ItemShopStock *getDemonFangShopData(uint32_t mapId, uint32_t shopIdx, uint32_t *numItems);

    // Cleanup when mod unloads
    void cleanupMod(WolfModId modId);
};