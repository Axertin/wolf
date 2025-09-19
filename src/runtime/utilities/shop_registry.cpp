#include "shop_registry.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

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

void ShopRegistry::addItemToShop(uint32_t mapId, WolfModId modId, int32_t itemType, int32_t cost)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    if (itemShops.find(mapId) == itemShops.end())
    {
        itemShops[mapId] = std::make_unique<ShopDefinition>();
    }

    itemShops[mapId]->addItem(modId, itemType, cost);
    logDebug("Added item %d (cost %d) to shop for map %u by mod %u", itemType, cost, mapId, modId);
}

void ShopRegistry::removeModItemsFromShop(uint32_t mapId, WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    auto shopIt = itemShops.find(mapId);
    if (shopIt != itemShops.end())
    {
        shopIt->second->removeModItems(modId);
    }
}

void ShopRegistry::setSellValueOverride(uint32_t mapId, int32_t itemType, int32_t sellValue)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    if (itemShops.find(mapId) == itemShops.end())
    {
        itemShops[mapId] = std::make_unique<ShopDefinition>();
    }

    itemShops[mapId]->setSellValueOverride(itemType, sellValue);
}

const uint8_t *ShopRegistry::getShopData(uint32_t mapId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    auto shopIt = itemShops.find(mapId);
    if (shopIt != itemShops.end())
    {
        return shopIt->second->getData();
    }

    return nullptr;
}

void ShopRegistry::addDemonFangItem(uint32_t mapId, WolfModId modId, int32_t itemType, int32_t cost)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    okami::ItemShopStock stock = {itemType, cost, 0};
    demonFangShops[mapId].emplace_back(stock);

    logDebug("Added demon fang item %d (cost %d) to map %u by mod %u", itemType, cost, mapId, modId);
}

void ShopRegistry::removeModDemonFangItems(uint32_t mapId, WolfModId modId)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    // Since we don't track mod ownership, clear the entire demon fang shop for this map
    auto shopIt = demonFangShops.find(mapId);
    if (shopIt != demonFangShops.end())
    {
        shopIt->second.clear();
    }
}

okami::ItemShopStock *ShopRegistry::getDemonFangShopData(uint32_t mapId, uint32_t *numItems)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    auto shopIt = demonFangShops.find(mapId);
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
    for (auto &[mapId, shop] : itemShops)
    {
        shop->removeModItems(modId);
    }

    // Clean up demon fang shops - since we don't track mod ownership, clear all
    for (auto &[mapId, items] : demonFangShops)
    {
        items.clear();
    }

    logDebug("Cleaned up shop items for mod %u", modId);
}
