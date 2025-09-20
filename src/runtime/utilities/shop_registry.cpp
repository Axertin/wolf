#include "shop_registry.h"

#include <algorithm>
#include <cstring>

#include "../core/memory_access.h"
#include "../wolf_runtime_api.h"
#include "logger.h"
#include "okami/maps.hpp"

// Constants from the old system
constexpr size_t MaxShopStockSize = 50;

//==============================================================================
// SHOP DEFINITION IMPLEMENTATION
//==============================================================================

ShopDefinition::ShopDefinition()
{
}

void ShopDefinition::rebuildISL()
{
    dataISL.clear();
    dataISL.reserve(sizeof(okami::ISLHeader) + sizeof(uint32_t) + sizeof(okami::ItemShopStock) * items.size() + sizeof(okami::SellValueArray));

    // ISL Header
    okami::ISLHeader header = {"ISL", 1, 0, 0};
    const uint8_t *headerBytes = reinterpret_cast<const uint8_t *>(&header);
    dataISL.insert(dataISL.end(), headerBytes, headerBytes + sizeof(header));

    // Number of items
    uint32_t numItems = static_cast<uint32_t>(items.size());
    const uint8_t *numItemsBytes = reinterpret_cast<const uint8_t *>(&numItems);
    dataISL.insert(dataISL.end(), numItemsBytes, numItemsBytes + sizeof(numItems));

    // Items
    for (const auto &item : items)
    {
        okami::ItemShopStock stock = {item.itemType, item.cost, 0}; // unk field set to 0
        const uint8_t *stockBytes = reinterpret_cast<const uint8_t *>(&stock);
        dataISL.insert(dataISL.end(), stockBytes, stockBytes + sizeof(stock));
    }

    // Sell values array
    const uint8_t *sellValuesBytes = reinterpret_cast<const uint8_t *>(sellValues.data());
    dataISL.insert(dataISL.end(), sellValuesBytes, sellValuesBytes + sizeof(sellValues));
}

void ShopDefinition::checkDirty()
{
    if (dirty)
    {
        rebuildISL();
        dirty = false;
    }
}

const uint8_t *ShopDefinition::getData()
{
    std::lock_guard<std::mutex> lock(itemsMutex);
    checkDirty();
    return dataISL.data();
}

void ShopDefinition::addItem(WolfModId modId, int32_t itemType, int32_t cost)
{
    std::lock_guard<std::mutex> lock(itemsMutex);

    if (items.size() >= MaxShopStockSize)
    {
        ::logWarning("Max shop stock size (%zu) exceeded, ignoring item %d", MaxShopStockSize, itemType);
        return;
    }

    items.emplace_back(ShopItem{itemType, cost, modId});
    dirty = true;
}

void ShopDefinition::removeModItems(WolfModId modId)
{
    std::lock_guard<std::mutex> lock(itemsMutex);

    auto newEnd = std::remove_if(items.begin(), items.end(), [modId](const ShopItem &item) { return item.modId == modId; });

    if (newEnd != items.end())
    {
        items.erase(newEnd, items.end());
        dirty = true;
    }
}

void ShopDefinition::clearAllItems()
{
    std::lock_guard<std::mutex> lock(itemsMutex);
    items.clear();
    dirty = true;
}

void ShopDefinition::setSellValueOverride(int32_t itemType, int32_t sellValue)
{
    std::lock_guard<std::mutex> lock(itemsMutex);

    if (itemType >= 0 && itemType < static_cast<int32_t>(sellValues.size()))
    {
        sellValues[itemType] = sellValue;
        dirty = true;
    }
}

void ShopDefinition::setSellValues(const okami::SellValueArray &replacementSellValues)
{
    std::lock_guard<std::mutex> lock(itemsMutex);
    sellValues = replacementSellValues;
    dirty = true;
}

std::vector<ShopItem> ShopDefinition::getModItems(WolfModId modId) const
{
    std::lock_guard<std::mutex> lock(itemsMutex);
    std::vector<ShopItem> modItems;

    std::copy_if(items.begin(), items.end(), std::back_inserter(modItems), [modId](const ShopItem &item) { return item.modId == modId; });

    return modItems;
}

//==============================================================================
// SHOP REGISTRY IMPLEMENTATION
//==============================================================================

ShopRegistry &ShopRegistry::instance()
{
    static ShopRegistry registry;
    return registry;
}

void ShopRegistry::initializeDefaultShops()
{
    std::lock_guard<std::mutex> lock(registryMutex);

    // Set up Taka Pass shops with special fish pricing
    // Map ID 0xF07 (TakaPassCursed) and 0xF08 (TakaPassHealed)
    uint32_t takaPassCursed = 0xF07;
    uint32_t takaPassHealed = 0xF08;

    // Create shop definitions if they don't exist and set special sell values
    uint64_t takaPassCursedKey = makeShopKey(takaPassCursed, 0);
    if (itemShops.find(takaPassCursedKey) == itemShops.end())
    {
        itemShops[takaPassCursedKey] = std::make_unique<ShopDefinition>();
    }
    itemShops[takaPassCursedKey]->setSellValues(okami::DefaultTakaPassItemSellPrices);

    uint64_t takaPassHealedKey = makeShopKey(takaPassHealed, 0);
    if (itemShops.find(takaPassHealedKey) == itemShops.end())
    {
        itemShops[takaPassHealedKey] = std::make_unique<ShopDefinition>();
    }
    itemShops[takaPassHealedKey]->setSellValues(okami::DefaultTakaPassItemSellPrices);

    // Set up Seian City fish shop with special pricing
    // Map ID 0x200 (SeianCityAristocraticQuarter) assuming fish shop is at index 1
    uint32_t seianAristocratic = 0x200;
    uint64_t seianFishShopKey = makeShopKey(seianAristocratic, 1); // Fish shop likely at index 1

    if (itemShops.find(seianFishShopKey) == itemShops.end())
    {
        itemShops[seianFishShopKey] = std::make_unique<ShopDefinition>();
    }
    itemShops[seianFishShopKey]->setSellValues(okami::DefaultSeianFishShopItemSellPrices);

    logDebug("[WOLF] Initialized default shop configurations with special fish pricing");
}

uint32_t ShopRegistry::getCurrentMapId() const
{
    // Read current exterior map ID from game memory (similar to original system)
    uintptr_t mainBase = wolfRuntimeGetModuleBase("main.dll");
    if (mainBase == 0)
        return 0;

    uint16_t mapId = 0;
    if (wolfRuntimeReadMemory(mainBase + okami::main::exteriorMapID, &mapId, sizeof(mapId)))
    {
        return static_cast<uint32_t>(mapId);
    }
    return 0;
}

const uint8_t *ShopRegistry::getCurrentItemShopData(uint32_t shopNum)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint32_t currentMapId = getCurrentMapId();
    if (currentMapId == 0)
        return nullptr;

    // Map-to-shop resolution logic (based on original GetCurrentItemShopData)
    switch (currentMapId)
    {
    // Kamiki Village (both cursed and healed versions)
    case 0x100: // KamikiVillageCursed
    case 0x102: // KamikiVillage
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }

    // Agata Forest
    case 0xF03: // AgataForestCursed
    case 0xF04: // AgataForestHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }

    // Taka Pass (both cursed and healed versions)
    case 0xF07: // TakaPassCursed
    case 0xF08: // TakaPassHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }

    // Seian City multi-shop area (special case)
    case 0x201: // SeianCityCommonersQuarter
    {
        uint64_t shopKey = makeShopKey(currentMapId, shopNum);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }

    // Seian City Aristocratic Quarter
    case 0x200: // SeianCityAristocraticQuarter
    {
        uint64_t shopKey = makeShopKey(currentMapId, shopNum);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }

        // Add more map cases as needed...
        // TODO: Add other shop locations from the game

    default:
    {
        // Generic fallback - try to find shop for this map + shopNum
        uint64_t shopKey = makeShopKey(currentMapId, shopNum);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    }

    // No custom shop found for this map/shopNum combination
    return nullptr;
}

void ShopRegistry::addItemToShop(uint32_t mapId, uint32_t shopIdx, WolfModId modId, int32_t itemType, int32_t cost)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    if (itemShops.find(shopKey) == itemShops.end())
    {
        itemShops[shopKey] = std::make_unique<ShopDefinition>();
    }

    itemShops[shopKey]->addItem(modId, itemType, cost);
    logDebug("Added item %d (cost %d) to shop for map %u, shop %u by mod %u", itemType, cost, mapId, shopIdx, modId);
}

void ShopRegistry::removeModItemsFromShop(uint32_t mapId, uint32_t shopIdx, WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = itemShops.find(shopKey);
    if (shopIt != itemShops.end())
    {
        shopIt->second->removeModItems(modId);
    }
}

void ShopRegistry::setSellValueOverride(uint32_t mapId, uint32_t shopIdx, int32_t itemType, int32_t sellValue)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    if (itemShops.find(shopKey) == itemShops.end())
    {
        itemShops[shopKey] = std::make_unique<ShopDefinition>();
    }

    itemShops[shopKey]->setSellValueOverride(itemType, sellValue);
}

const uint8_t *ShopRegistry::getShopData(uint32_t mapId, uint32_t shopIdx)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = itemShops.find(shopKey);
    if (shopIt != itemShops.end())
    {
        return shopIt->second->getData();
    }

    return nullptr;
}

void ShopRegistry::addDemonFangItem(uint32_t mapId, uint32_t shopIdx, WolfModId modId, int32_t itemType, int32_t cost)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    okami::ItemShopStock stock = {itemType, cost, 0};
    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    demonFangShops[shopKey].emplace_back(stock);

    logDebug("Added demon fang item %d (cost %d) to map %u, shop %u by mod %u", itemType, cost, mapId, shopIdx, modId);
}

void ShopRegistry::removeModDemonFangItems(uint32_t mapId, uint32_t shopIdx, WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    // Since we don't track mod ownership, clear the entire demon fang shop for this map/shop
    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = demonFangShops.find(shopKey);
    if (shopIt != demonFangShops.end())
    {
        shopIt->second.clear();
    }
}

okami::ItemShopStock *ShopRegistry::getDemonFangShopData(uint32_t mapId, uint32_t shopIdx, uint32_t *numItems)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = demonFangShops.find(shopKey);
    if (shopIt != demonFangShops.end() && !shopIt->second.empty())
    {
        *numItems = static_cast<uint32_t>(shopIt->second.size());
        return shopIt->second.data();
    }

    *numItems = 0;
    return nullptr;
}

void ShopRegistry::cleanupMod(WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    // Clean up item shops
    for (auto &[shopKey, shop] : itemShops)
    {
        shop->removeModItems(modId);
    }

    // Clean up demon fang shops - since we don't track mod ownership, clear all
    for (auto &[shopKey, items] : demonFangShops)
    {
        items.clear();
    }

    logDebug("Cleaned up shop items for mod %u", modId);
}
