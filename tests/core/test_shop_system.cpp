#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/mod_lifecycle.h"
#include "core/shop_system.h"
#include "test_mocks.h"
#include "wolf_types.h"

// Access to real global shop state
extern std::mutex g_ShopCallbackMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ShopCallbacks>> g_ModShopCallbacks;

// Access to real global mod state (declared in mod_lifecycle.h)
extern std::recursive_mutex g_ModMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;

// Forward declarations for functions we're testing
extern "C"
{
    void wolfRuntimeAddShopItem(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost);
    void wolfRuntimeAddDemonFangItem(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost);
    void wolfRuntimeSetSellValue(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t sell_value);
    void wolfRuntimeRemoveModShopItems(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx);
    void wolfRuntimeRemoveModDemonFangItems(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx);
    void wolfRuntimeCleanupModShops(WolfModId mod_id);
    void wolfRuntimeRegisterShopPurchase(WolfModId mod_id, WolfShopPurchaseCallback callback, void *userdata);
    void wolfRuntimeRegisterShopInteract(WolfModId mod_id, WolfShopInteractCallback callback, void *userdata);
}

// Test fixture for shop system tests
class ShopSystemTestFixture
{
  public:
    ShopSystemTestFixture()
    {
        // Clear real global mod state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
        }

        // Clear real global shop callback state
        {
            std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
            g_ModShopCallbacks.clear();
        }
    }

    ~ShopSystemTestFixture()
    {
        // Clean up global state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
        }

        {
            std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
            g_ModShopCallbacks.clear();
        }
    }

    // Helper to add test mods to real global state
    WolfModId addTestMod(const std::string &name)
    {
        auto modInfo = std::make_unique<ModInfo>();
        modInfo->name = name;
        modInfo->version = "1.0.0";

        std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
        WolfModId id = static_cast<WolfModId>(g_Mods.size() + 1);
        g_Mods[id] = std::move(modInfo);
        return id;
    }
};

TEST_CASE_METHOD(ShopSystemTestFixture, "Shop system basic item management", "[shop][core]")
{
    WolfModId testMod = addTestMod("TestShopMod");
    uint32_t testMapId = 0x102; // KamikiVillage

    SECTION("Add shop items")
    {
        REQUIRE_NOTHROW(wolfRuntimeAddShopItem(testMod, testMapId, 0, 10, 100)); // item type 10, cost 100
        REQUIRE_NOTHROW(wolfRuntimeAddShopItem(testMod, testMapId, 0, 15, 250)); // item type 15, cost 250
    }

    SECTION("Add demon fang items")
    {
        REQUIRE_NOTHROW(wolfRuntimeAddDemonFangItem(testMod, testMapId, 0, 20, 5));  // item type 20, cost 5 fangs
        REQUIRE_NOTHROW(wolfRuntimeAddDemonFangItem(testMod, testMapId, 0, 25, 10)); // item type 25, cost 10 fangs
    }

    SECTION("Set sell values")
    {
        REQUIRE_NOTHROW(wolfRuntimeSetSellValue(testMod, testMapId, 0, 10, 50));  // item type 10 sells for 50
        REQUIRE_NOTHROW(wolfRuntimeSetSellValue(testMod, testMapId, 0, 15, 125)); // item type 15 sells for 125
    }

    SECTION("Remove mod items")
    {
        // Add some items first
        wolfRuntimeAddShopItem(testMod, testMapId, 0, 10, 100);
        wolfRuntimeAddDemonFangItem(testMod, testMapId, 0, 20, 5);

        // Remove them
        REQUIRE_NOTHROW(wolfRuntimeRemoveModShopItems(testMod, 0, testMapId));
        REQUIRE_NOTHROW(wolfRuntimeRemoveModDemonFangItems(testMod, 0, testMapId));
    }

    SECTION("Cleanup all mod shops")
    {
        // Add items to multiple maps
        wolfRuntimeAddShopItem(testMod, testMapId, 0, 10, 100);
        wolfRuntimeAddShopItem(testMod, 0x103, 0, 15, 200); // Different map
        wolfRuntimeAddDemonFangItem(testMod, testMapId, 0, 20, 5);

        // Clean up everything for this mod
        REQUIRE_NOTHROW(wolfRuntimeCleanupModShops(testMod));
    }
}

TEST_CASE_METHOD(ShopSystemTestFixture, "Shop system callback registration", "[shop][core]")
{
    WolfModId testMod = addTestMod("TestCallbackMod");

    // Test callback storage
    struct CallbackTestData
    {
        int purchaseCount = 0;
        int interactCount = 0;
        int lastShopType = -1;
        void *lastShopStruct = nullptr;
    };

    CallbackTestData testData;

    auto purchaseCallback = [](int shop_type, void *shop_struct, void *userdata)
    {
        auto *data = static_cast<CallbackTestData *>(userdata);
        data->purchaseCount++;
        data->lastShopType = shop_type;
        data->lastShopStruct = shop_struct;
    };

    auto interactCallback = [](int shop_type, void *shop_struct, void *userdata)
    {
        auto *data = static_cast<CallbackTestData *>(userdata);
        data->interactCount++;
        data->lastShopType = shop_type;
        data->lastShopStruct = shop_struct;
    };

    SECTION("Register purchase callback")
    {
        REQUIRE_NOTHROW(wolfRuntimeRegisterShopPurchase(testMod, purchaseCallback, &testData));

        // Verify callback was registered
        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        REQUIRE(g_ModShopCallbacks.find(testMod) != g_ModShopCallbacks.end());
        REQUIRE(g_ModShopCallbacks[testMod]->purchaseCallbacks.size() == 1);
    }

    SECTION("Register interact callback")
    {
        REQUIRE_NOTHROW(wolfRuntimeRegisterShopInteract(testMod, interactCallback, &testData));

        // Verify callback was registered
        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        REQUIRE(g_ModShopCallbacks.find(testMod) != g_ModShopCallbacks.end());
        REQUIRE(g_ModShopCallbacks[testMod]->interactCallbacks.size() == 1);
    }

    SECTION("Register multiple callbacks")
    {
        wolfRuntimeRegisterShopPurchase(testMod, purchaseCallback, &testData);
        wolfRuntimeRegisterShopInteract(testMod, interactCallback, &testData);

        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        REQUIRE(g_ModShopCallbacks[testMod]->purchaseCallbacks.size() == 1);
        REQUIRE(g_ModShopCallbacks[testMod]->interactCallbacks.size() == 1);
    }

    SECTION("Multiple mods with callbacks")
    {
        WolfModId testMod2 = addTestMod("TestCallbackMod2");
        CallbackTestData testData2;

        wolfRuntimeRegisterShopPurchase(testMod, purchaseCallback, &testData);
        wolfRuntimeRegisterShopPurchase(testMod2, purchaseCallback, &testData2);

        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        REQUIRE(g_ModShopCallbacks.size() == 2);
        REQUIRE(g_ModShopCallbacks[testMod]->purchaseCallbacks.size() == 1);
        REQUIRE(g_ModShopCallbacks[testMod2]->purchaseCallbacks.size() == 1);
    }
}

TEST_CASE_METHOD(ShopSystemTestFixture, "Shop system callback invocation", "[shop][core]")
{
    WolfModId testMod = addTestMod("TestInvokeMod");

    // Test callback invocation
    struct CallbackTestData
    {
        int purchaseCount = 0;
        int interactCount = 0;
        int lastShopType = -1;
        void *lastShopStruct = nullptr;
    };

    CallbackTestData testData;

    auto purchaseCallback = [](int shop_type, void *shop_struct, void *userdata)
    {
        auto *data = static_cast<CallbackTestData *>(userdata);
        data->purchaseCount++;
        data->lastShopType = shop_type;
        data->lastShopStruct = shop_struct;
    };

    auto interactCallback = [](int shop_type, void *shop_struct, void *userdata)
    {
        auto *data = static_cast<CallbackTestData *>(userdata);
        data->interactCount++;
        data->lastShopType = shop_type;
        data->lastShopStruct = shop_struct;
    };

    // Register callbacks
    wolfRuntimeRegisterShopPurchase(testMod, purchaseCallback, &testData);
    wolfRuntimeRegisterShopInteract(testMod, interactCallback, &testData);

    SECTION("Invoke purchase callback")
    {
        void *dummyShopStruct = reinterpret_cast<void *>(0x12345678);

        wolf::runtime::internal::callShopPurchase(0, dummyShopStruct); // Item shop

        REQUIRE(testData.purchaseCount == 1);
        REQUIRE(testData.lastShopType == 0);
        REQUIRE(testData.lastShopStruct == dummyShopStruct);
    }

    SECTION("Invoke interact callback")
    {
        void *dummyShopStruct = reinterpret_cast<void *>(0x87654321);

        wolf::runtime::internal::callShopInteract(1, dummyShopStruct); // Demon fang shop

        REQUIRE(testData.interactCount == 1);
        REQUIRE(testData.lastShopType == 1);
        REQUIRE(testData.lastShopStruct == dummyShopStruct);
    }

    SECTION("Multiple callback invocations")
    {
        void *shopStruct1 = reinterpret_cast<void *>(0x11111111);
        void *shopStruct2 = reinterpret_cast<void *>(0x22222222);

        wolf::runtime::internal::callShopPurchase(0, shopStruct1);
        wolf::runtime::internal::callShopPurchase(1, shopStruct2);
        wolf::runtime::internal::callShopInteract(2, shopStruct1);

        REQUIRE(testData.purchaseCount == 2);
        REQUIRE(testData.interactCount == 1);
        REQUIRE(testData.lastShopType == 2); // Last was skill shop interact
        REQUIRE(testData.lastShopStruct == shopStruct1);
    }
}

TEST_CASE_METHOD(ShopSystemTestFixture, "Shop system error handling", "[shop][core]")
{
    WolfModId testMod = addTestMod("TestErrorMod");

    SECTION("Null callback registration")
    {
        // Should not crash with null callbacks
        REQUIRE_NOTHROW(wolfRuntimeRegisterShopPurchase(testMod, nullptr, nullptr));
        REQUIRE_NOTHROW(wolfRuntimeRegisterShopInteract(testMod, nullptr, nullptr));

        // Should not have registered anything
        std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
        REQUIRE(g_ModShopCallbacks.find(testMod) == g_ModShopCallbacks.end());
    }

    SECTION("Callback exception handling")
    {
        struct TestException
        {
        };

        auto throwingCallback = [](int, void *, void *) { throw TestException(); };

        wolfRuntimeRegisterShopPurchase(testMod, throwingCallback, nullptr);

        // Should not crash when callback throws
        REQUIRE_NOTHROW(wolf::runtime::internal::callShopPurchase(0, nullptr));
    }

    SECTION("Invalid mod ID operations")
    {
        WolfModId invalidMod = 99999;

        // Should not crash with invalid mod ID
        REQUIRE_NOTHROW(wolfRuntimeAddShopItem(invalidMod, 0x102, 0, 10, 100));
        REQUIRE_NOTHROW(wolfRuntimeCleanupModShops(invalidMod));
        REQUIRE_NOTHROW(wolfRuntimeRegisterShopPurchase(invalidMod, nullptr, nullptr));
    }
}

TEST_CASE_METHOD(ShopSystemTestFixture, "Shop system cleanup on mod unload", "[shop][core]")
{
    WolfModId testMod1 = addTestMod("TestCleanupMod1");
    WolfModId testMod2 = addTestMod("TestCleanupMod2");

    // Test callback storage
    struct CallbackTestData
    {
        int callCount = 0;
    };

    CallbackTestData testData1, testData2;

    auto callback1 = [](int, void *, void *userdata) { static_cast<CallbackTestData *>(userdata)->callCount++; };

    auto callback2 = [](int, void *, void *userdata) { static_cast<CallbackTestData *>(userdata)->callCount++; };

    // Register callbacks for both mods
    wolfRuntimeRegisterShopPurchase(testMod1, callback1, &testData1);
    wolfRuntimeRegisterShopPurchase(testMod2, callback2, &testData2);

    // Add shop items for both mods
    wolfRuntimeAddShopItem(testMod1, 0x102, 0, 10, 100);
    wolfRuntimeAddShopItem(testMod2, 0x102, 0, 15, 200);

    SECTION("Cleanup specific mod")
    {
        // Cleanup mod 1
        wolfRuntimeCleanupModShops(testMod1);

        // Verify mod 1 callbacks are removed but mod 2 remains
        {
            std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
            REQUIRE(g_ModShopCallbacks.find(testMod1) == g_ModShopCallbacks.end());
            REQUIRE(g_ModShopCallbacks.find(testMod2) != g_ModShopCallbacks.end());
        }

        // Test that only mod 2 callback is called
        wolf::runtime::internal::callShopPurchase(0, nullptr);
        REQUIRE(testData1.callCount == 0);
        REQUIRE(testData2.callCount == 1);
    }

    SECTION("Cleanup all mods")
    {
        wolfRuntimeCleanupModShops(testMod1);
        wolfRuntimeCleanupModShops(testMod2);

        // Verify all callbacks are removed
        {
            std::lock_guard<std::mutex> lock(g_ShopCallbackMutex);
            REQUIRE(g_ModShopCallbacks.empty());
        }

        // Test that no callbacks are called
        wolf::runtime::internal::callShopPurchase(0, nullptr);
        REQUIRE(testData1.callCount == 0);
        REQUIRE(testData2.callCount == 0);
    }
}
