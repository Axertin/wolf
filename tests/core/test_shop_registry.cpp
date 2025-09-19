#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "okami/shopdata.h"
#include "utilities/shop_registry.h"
#include "wolf_types.h"

// Test fixture for shop registry tests
class ShopRegistryTestFixture
{
  public:
    ShopRegistryTestFixture()
    {
        // Get a fresh registry instance and clean it up
        ShopRegistry::instance().cleanupMod(1);
        ShopRegistry::instance().cleanupMod(2);
        ShopRegistry::instance().cleanupMod(3);
    }

    ~ShopRegistryTestFixture()
    {
        // Clean up after tests
        ShopRegistry::instance().cleanupMod(1);
        ShopRegistry::instance().cleanupMod(2);
        ShopRegistry::instance().cleanupMod(3);
    }
};

TEST_CASE_METHOD(ShopRegistryTestFixture, "ShopDefinition basic functionality", "[shop][registry][core]")
{
    ShopDefinition shopDef;

    SECTION("Empty shop initially")
    {
        const uint8_t *data = shopDef.getData();
        REQUIRE(data != nullptr);

        // Should have ISL header + 0 items + sell values array
        // ISL header: 4 bytes magic + 12 bytes other fields = 16 bytes
        // Number of items: 4 bytes
        // Sell values array: sizeof(okami::SellValueArray)
        size_t expectedSize = sizeof(okami::ISLHeader) + sizeof(uint32_t) + sizeof(okami::SellValueArray);

        // Verify ISL header
        const okami::ISLHeader *header = reinterpret_cast<const okami::ISLHeader *>(data);
        REQUIRE(std::strncmp(header->magic, "ISL", 3) == 0);
        REQUIRE(header->variations == 1);

        // Verify zero items
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 0);
    }

    SECTION("Add single item")
    {
        WolfModId testMod = 1;
        shopDef.addItem(testMod, 10, 100); // item type 10, cost 100

        const uint8_t *data = shopDef.getData();
        REQUIRE(data != nullptr);

        // Verify ISL header
        const okami::ISLHeader *header = reinterpret_cast<const okami::ISLHeader *>(data);
        REQUIRE(std::strncmp(header->magic, "ISL", 3) == 0);

        // Verify one item
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 1);

        // Verify item data
        const okami::ItemShopStock *items = reinterpret_cast<const okami::ItemShopStock *>(data + sizeof(okami::ISLHeader) + sizeof(uint32_t));
        REQUIRE(items[0].itemType == 10);
        REQUIRE(items[0].cost == 100);
        REQUIRE(items[0].unk == 0);
    }

    SECTION("Add multiple items")
    {
        WolfModId testMod = 1;
        shopDef.addItem(testMod, 10, 100);
        shopDef.addItem(testMod, 15, 250);
        shopDef.addItem(testMod, 20, 500);

        const uint8_t *data = shopDef.getData();

        // Verify three items
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 3);

        // Verify item data
        const okami::ItemShopStock *items = reinterpret_cast<const okami::ItemShopStock *>(data + sizeof(okami::ISLHeader) + sizeof(uint32_t));
        REQUIRE(items[0].itemType == 10);
        REQUIRE(items[0].cost == 100);
        REQUIRE(items[1].itemType == 15);
        REQUIRE(items[1].cost == 250);
        REQUIRE(items[2].itemType == 20);
        REQUIRE(items[2].cost == 500);
    }

    SECTION("Clear items")
    {
        WolfModId testMod = 1;
        shopDef.addItem(testMod, 10, 100);
        shopDef.addItem(testMod, 15, 250);

        shopDef.clearAllItems();

        const uint8_t *data = shopDef.getData();
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 0);
    }

    SECTION("Remove mod items")
    {
        WolfModId testMod1 = 1;
        WolfModId testMod2 = 2;

        shopDef.addItem(testMod1, 10, 100);
        shopDef.addItem(testMod2, 15, 250);
        shopDef.addItem(testMod1, 20, 500);

        // Remove mod1 items
        shopDef.removeModItems(testMod1);

        const uint8_t *data = shopDef.getData();
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 1);

        // Only mod2 item should remain
        const okami::ItemShopStock *items = reinterpret_cast<const okami::ItemShopStock *>(data + sizeof(okami::ISLHeader) + sizeof(uint32_t));
        REQUIRE(items[0].itemType == 15);
        REQUIRE(items[0].cost == 250);
    }

    SECTION("Max shop stock limit")
    {
        WolfModId testMod = 1;

        // Add up to the limit (50 items)
        for (int i = 0; i < 50; i++)
        {
            shopDef.addItem(testMod, i, i * 10);
        }

        const uint8_t *data = shopDef.getData();
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 50);

        // Adding beyond limit should be ignored
        shopDef.addItem(testMod, 100, 1000);

        data = shopDef.getData();
        numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 50); // Should still be 50
    }
}

TEST_CASE_METHOD(ShopRegistryTestFixture, "ShopDefinition sell value override", "[shop][registry][core]")
{
    ShopDefinition shopDef;

    SECTION("Set sell value override")
    {
        shopDef.setSellValueOverride(10, 75); // Item type 10 sells for 75

        const uint8_t *data = shopDef.getData();

        // Sell values are at the end: after header + numItems + items
        size_t sellValuesOffset = sizeof(okami::ISLHeader) + sizeof(uint32_t); // No items, so skip items
        const okami::SellValueArray *sellValues = reinterpret_cast<const okami::SellValueArray *>(data + sellValuesOffset);

        REQUIRE((*sellValues)[10] == 75);
    }

    SECTION("Set sell values array")
    {
        okami::SellValueArray customSellValues = okami::DefaultItemSellPrices;
        customSellValues[5] = 25;
        customSellValues[10] = 50;
        customSellValues[15] = 100;

        shopDef.setSellValues(customSellValues);

        const uint8_t *data = shopDef.getData();
        size_t sellValuesOffset = sizeof(okami::ISLHeader) + sizeof(uint32_t);
        const okami::SellValueArray *sellValues = reinterpret_cast<const okami::SellValueArray *>(data + sellValuesOffset);

        REQUIRE((*sellValues)[5] == 25);
        REQUIRE((*sellValues)[10] == 50);
        REQUIRE((*sellValues)[15] == 100);
    }

    SECTION("Invalid item type for sell value")
    {
        // Should handle out-of-bounds gracefully
        REQUIRE_NOTHROW(shopDef.setSellValueOverride(-1, 100));
        REQUIRE_NOTHROW(shopDef.setSellValueOverride(99999, 100));
    }
}

TEST_CASE_METHOD(ShopRegistryTestFixture, "ShopRegistry item shop management", "[shop][registry][core]")
{
    ShopRegistry &registry = ShopRegistry::instance();
    uint32_t testMapId = 0x102;
    WolfModId testMod = 1;

    SECTION("Add items to shop")
    {
        registry.addItemToShop(testMapId, testMod, 10, 100);
        registry.addItemToShop(testMapId, testMod, 15, 250);

        const uint8_t *data = registry.getShopData(testMapId);
        REQUIRE(data != nullptr);

        // Verify items were added
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 2);
    }

    SECTION("Multiple mods adding to same shop")
    {
        WolfModId testMod1 = 1;
        WolfModId testMod2 = 2;

        registry.addItemToShop(testMapId, testMod1, 10, 100);
        registry.addItemToShop(testMapId, testMod2, 15, 250);
        registry.addItemToShop(testMapId, testMod1, 20, 500);

        const uint8_t *data = registry.getShopData(testMapId);
        REQUIRE(data != nullptr);

        // All items should be present
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 3);
    }

    SECTION("Remove mod items from shop")
    {
        WolfModId testMod1 = 1;
        WolfModId testMod2 = 2;

        registry.addItemToShop(testMapId, testMod1, 10, 100);
        registry.addItemToShop(testMapId, testMod2, 15, 250);

        registry.removeModItemsFromShop(testMapId, testMod1);

        const uint8_t *data = registry.getShopData(testMapId);
        const uint32_t *numItems = reinterpret_cast<const uint32_t *>(data + sizeof(okami::ISLHeader));
        REQUIRE(*numItems == 1); // Only mod2 item should remain
    }

    SECTION("Nonexistent shop returns null")
    {
        const uint8_t *data = registry.getShopData(0x999); // Non-existent map
        REQUIRE(data == nullptr);
    }
}

TEST_CASE_METHOD(ShopRegistryTestFixture, "ShopRegistry demon fang shop management", "[shop][registry][core]")
{
    ShopRegistry &registry = ShopRegistry::instance();
    uint32_t testMapId = 0x102;
    WolfModId testMod = 1;

    SECTION("Add demon fang items")
    {
        registry.addDemonFangItem(testMapId, testMod, 10, 5);
        registry.addDemonFangItem(testMapId, testMod, 15, 10);

        uint32_t numItems = 0;
        okami::ItemShopStock *data = registry.getDemonFangShopData(testMapId, &numItems);

        REQUIRE(data != nullptr);
        REQUIRE(numItems == 2);
        REQUIRE(data[0].itemType == 10);
        REQUIRE(data[0].cost == 5);
        REQUIRE(data[1].itemType == 15);
        REQUIRE(data[1].cost == 10);
    }

    SECTION("Remove mod demon fang items")
    {
        WolfModId testMod1 = 1;
        WolfModId testMod2 = 2;

        registry.addDemonFangItem(testMapId, testMod1, 10, 5);
        registry.addDemonFangItem(testMapId, testMod2, 15, 10);

        registry.removeModDemonFangItems(testMapId, testMod1);

        uint32_t numItems = 0;
        okami::ItemShopStock *data = registry.getDemonFangShopData(testMapId, &numItems);

        REQUIRE(numItems == 0); // Entire shop cleared since we don't track per-mod
    }

    SECTION("Nonexistent demon fang shop")
    {
        uint32_t numItems = 999;
        okami::ItemShopStock *data = registry.getDemonFangShopData(0x999, &numItems);

        REQUIRE(data == nullptr);
        REQUIRE(numItems == 0);
    }
}

TEST_CASE_METHOD(ShopRegistryTestFixture, "ShopRegistry cleanup", "[shop][registry][core]")
{
    ShopRegistry &registry = ShopRegistry::instance();
    uint32_t testMapId1 = 0x102;
    uint32_t testMapId2 = 0x103;
    WolfModId testMod1 = 1;
    WolfModId testMod2 = 2;

    SECTION("Cleanup mod removes all data")
    {
        // Add data for both mods on multiple maps
        registry.addItemToShop(testMapId1, testMod1, 10, 100);
        registry.addItemToShop(testMapId2, testMod1, 15, 250);
        registry.addItemToShop(testMapId1, testMod2, 20, 500);

        registry.addDemonFangItem(testMapId1, testMod1, 30, 5);
        registry.addDemonFangItem(testMapId2, testMod2, 35, 10);

        // Cleanup mod1
        registry.cleanupMod(testMod1);

        // Verify mod1 data is gone but mod2 remains
        const uint8_t *shopData1 = registry.getShopData(testMapId1);
        const uint32_t *numItems1 = reinterpret_cast<const uint32_t *>(shopData1 + sizeof(okami::ISLHeader));
        REQUIRE(*numItems1 == 1); // Only mod2 item

        const uint8_t *shopData2 = registry.getShopData(testMapId2);
        if (shopData2 != nullptr) {
            // Shop exists but should be empty
            const uint32_t *numItems2 = reinterpret_cast<const uint32_t *>(shopData2 + sizeof(okami::ISLHeader));
            REQUIRE(*numItems2 == 0);
        }

        uint32_t numFangItems = 0;
        okami::ItemShopStock *fangData = registry.getDemonFangShopData(testMapId1, &numFangItems);
        REQUIRE(numFangItems == 0); // All demon fang items cleared

        fangData = registry.getDemonFangShopData(testMapId2, &numFangItems);
        REQUIRE(numFangItems == 0); // All demon fang items cleared
    }
}
