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

    // ISL Header - using original 2-field format
    okami::ISLHeader header = {"ISL", 1};
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
    // Map ID 0x201 (SeianCityCommonersQuarter) shop index 1 is the fish shop
    uint32_t seianCommoners = 0x201;
    uint64_t seianFishShopKey = makeShopKey(seianCommoners, 1); // Fish shop at index 1

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
    // Following the golden implementation from okami-apclient/src/client/shops.cpp:76-139
    switch (currentMapId)
    {
    case 0xF03: // AgataForestCursed
    case 0xF04: // AgataForestHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x307: // ArkofYamato
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x105: // CityCheckpoint
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x203: // DragonPalace
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x112: // KamikiVillagePostTei
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x100: // KamikiVillageCursed
    case 0x102: // KamikiVillage
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x302: // KamikiVillagePast
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0xF11: // KamuiCursed
    case 0xF12: // KamuiHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x108: // KusaVillage
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x110: // MoonCaveInterior
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x111: // MoonCaveStaircaseAndOrochiArena
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0xF0C: // NRyoshimaCoast
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x208: // OniIslandLowerInterior
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x305: // Ponctan
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0xF09: // RyoshimaCoastCursed
    case 0xF0A: // RyoshimaCoastHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x109: // SasaSanctuary
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x201: // SeianCityCommonersQuarter (multi-shop area)
    {
        uint64_t shopKey = makeShopKey(currentMapId, shopNum);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0xF01: // ShinshuFieldCursed
    case 0xF02: // ShinshuFieldHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0xF07: // TakaPassCursed
    case 0xF08: // TakaPassHealed
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
    case 0x303: // WawkuShrine
    {
        uint64_t shopKey = makeShopKey(currentMapId, 0);
        auto shopIt = itemShops.find(shopKey);
        if (shopIt != itemShops.end())
            return shopIt->second->getData();
        break;
    }
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
    demonFangShops_[shopKey][modId].emplace_back(stock);

    logDebug("Added demon fang item %d (cost %d) to map %u, shop %u by mod %u", itemType, cost, mapId, shopIdx, modId);
}

void ShopRegistry::removeModDemonFangItems(uint32_t mapId, uint32_t shopIdx, WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = demonFangShops_.find(shopKey);
    if (shopIt != demonFangShops_.end())
    {
        shopIt->second.erase(modId); // Remove only this mod's items
    }
}

okami::ItemShopStock *ShopRegistry::getDemonFangShopData(uint32_t mapId, uint32_t shopIdx, uint32_t *numItems)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    uint64_t shopKey = makeShopKey(mapId, shopIdx);
    auto shopIt = demonFangShops_.find(shopKey);
    if (shopIt != demonFangShops_.end())
    {
        // Combine all mods' items into a single vector
        static thread_local std::vector<okami::ItemShopStock> combinedItems;
        combinedItems.clear();
        
        for (const auto& modItems : shopIt->second)
        {
            const auto& items = modItems.second;
            combinedItems.insert(combinedItems.end(), items.begin(), items.end());
        }
        
        if (!combinedItems.empty())
        {
            *numItems = static_cast<uint32_t>(combinedItems.size());
            return combinedItems.data();
        }
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

    // Clean up demon fang shops - remove only this mod's items
    for (auto &[shopKey, modItemsMap] : demonFangShops_)
    {
        modItemsMap.erase(modId);
    }

    logDebug("Cleaned up shop items for mod %u", modId);
}
