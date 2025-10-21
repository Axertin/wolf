# Shop System Implementation

## Overview

The shop system provides mod access to Okami HD's three shop types (item shops, demon fang shops, skill shops) through memory hooks and data replacement.

## Critical Timing Requirements

**All shop data must be registered during mod initialization, before the game completes its startup sequence.**

The game allocates and caches shop data during initialization. Once the game fully initializes:
1. Resource buffers are allocated and fixed
2. Shop data is loaded from game files or our replacements
3. MSD (text) data is compiled and pointer-locked
4. The game begins normal operation

**Consequence**: Shop modifications registered after game init will not take effect properly, and may cause crashes in extreme cases.

### Initialization Sequence

1. **Mod Load** - Mods perform all shop-related edits like adding or removing items, changing prices, and setting icons during `earlyGameInit()`
2. **Shop Registry Build** - Registry compiles ISL binaries for each modified shop
3. **Game Init Begins** - Okami starts loading resources
4. **Hook Interception** - Our hooks provide custom data when requested
5. **Game Init Completes** - All buffers allocated, pointers locked
6. **Normal Operation** - Shops use the data we provided during init

## ISL Data Format

ISL (Item Shop List) is the binary format Okami uses for item shop inventory.

### Structure Layout

```cpp
#pragma pack(push, 1)
struct ISLHeader {
    char magic[4];        // "ISL\0"
    int32_t variations;   // Always 1
    int32_t unk2;         // Unknown field
    int32_t unk3;         // Unknown field
};

struct ItemShopStock {
    int32_t itemType;     // Item ID from ItemTypes enum
    int32_t cost;         // Purchase price in yen
    int32_t unk;          // Unknown field (set to 0)
};

// Complete ISL structure in memory:
// ISLHeader (16 bytes)
// uint32_t numItems (4 bytes)
// ItemShopStock items[numItems] (12 bytes each)
// SellValueArray sellValues (808 bytes - 202 int32_t values)
#pragma pack(pop)
```

### Building ISL Data

When a shop is modified:
1. `ShopDefinition::rebuildISL()` constructs the binary
2. Header is written with magic="ISL", variations=1
3. Item count written as uint32_t
4. Each `ItemShopStock` entry written sequentially
5. Full 808-byte sell value array appended
6. Data marked dirty=false, ready for game to use

### Sell Value Arrays

Sell values are indexed by item type (202 entries). Special shops have different arrays:
- `DefaultItemSellPrices` - Standard shops
- `DefaultTakaPassItemSellPrices` - Taka Pass shops
- `DefaultSeianFishShopItemSellPrices` - Seian fish shop

Arrays initialized during `ShopRegistry::initializeDefaultShops()`.

## Hook Points and Game Interaction

All hooks are installed via MinHook at fixed main.dll offsets. Hook addresses are from the tested okami-apclient implementation and must not change.

### Shop Variation Hook

**Address**: `main.dll + 0x4420C0`
**Function**: `onGetShopVariation`
**Purpose**: Disable shop variations

```cpp
int64_t __fastcall onGetShopVariation(void *unknown, uint32_t shopNum, char **shopTextureName)
{
    // Call metadata function but discard results
    uint32_t numEvents;
    oGetShopMetadata(unknown, shopNum, &numEvents, shopTextureName);
    return 0;  // Always return 0 to disable variations
}
```

**Why**: Variations require multiple duplicate shop definitions differing by 1-2 items. By returning 0, we eliminate this requirement.

### Resource Loading Hook

**Address**: `main.dll + 0x1B1770`
**Function**: `onLoadRsc`
**Purpose**: Intercept ISL/SSL resource requests

When the game calls `LoadRsc(rscPackage, "ISL", idx)`:
1. Get current map ID from `main.dll + 0xB6B240`
2. Query `ShopRegistry` for custom data at (mapId, idx)
3. If custom data exists, return pointer to our ISL binary
4. Otherwise, call original `oLoadRsc()` for vanilla data

**Critical**: The returned pointer must remain valid for the game's lifetime. `ShopDefinition` maintains the data in `std::vector<uint8_t> dataISL`.

### Shop Update Hooks

Three shop types have parallel hook structures.

#### Item Shop: UpdatePurchaseList

**Address**: `main.dll + 0x43E250`
**Function**: `onCItemShop_UpdatePurchaseList`

**Blocking interception** (original not called). For each shop slot:

```cpp
// Get inventory from game singleton
auto* collections = reinterpret_cast<okami::CollectionData*>(mainBase + 0xB205D0);

for (uint32_t i = 0; i < pShop->numSlots; i++) {
    int32_t itemType = pShop->itemStockList[i].itemType;

    pShop->shopSlots[i].itemType = itemType;
    pShop->shopSlots[i].pIcon = GetItemIcon(pShop, itemType);
    pShop->shopSlots[i].itemNameStrId = itemType + 294;  // Text ID formula
    pShop->shopSlots[i].maxCount = 1;
    pShop->shopSlots[i].currentCount = collections->inventory[itemType];
    pShop->shopSlots[i].itemCost = pShop->itemStockList[i].cost;
}
```

Original function has hardcoded assumptions about vanilla shop structure. Our custom shops may have different item counts and arrangements.

#### Item Shop: Purchase

**Address**: `main.dll + 0x43CA30`
**Function**: `onCItemShop_PurchaseItem`

Wrapper hook (original called):
1. Extract purchase info (item type, cost, slot index)
2. Call `oCItemShop_PurchaseItem()` to perform actual purchase
3. Trigger mod callbacks via `wolf::runtime::internal::callShopPurchase(0, pShop)`

The game's purchase logic handles yen deduction, inventory addition, sound effects, and UI updates. We only need to observe the purchase.

### Icon System

**Address**: `main.dll + 0x43BDA0`
**Function**: `GetItemIcon` (hooks `cItemShop::GetItemIcon`)

```cpp
hx::Texture *__fastcall GetItemIcon(okami::cItemShop *pShop, int item)
{
    if (item == 0 || !pShop->pIconsRsc) {
        return oCItemShop_GetItemIcon(pShop, item);  // Blank icon
    }

    // Index into custom icon package (first item is index 1)
    return reinterpret_cast<hx::Texture *>(oLoadRscIdx(pShop->pIconsRsc, item + 1));
}
```

`onLoadResourcePackageAsync` intercepts `"id/ItemShopBuyIcon.dat"` and redirects to whatever resource a mod has provided the path to.

This allows custom icons for modded items without modifying game files.

### Texture Limit Expansion

**Address**: `main.dll + 0x1412B0`
**Function**: `onGXTextureManager_GetNumEntries`

```cpp
int64_t __fastcall onGXTextureManager_GetNumEntries(void *textureManager, int32_t texGroup)
{
    if (texGroup == 4) {
        return 300;  // Increased from vanilla 128
    }
    return oGXTextureManager_GetNumEntries(textureManager, texGroup);
}
```

Texture group 4 is used for shop icons, and is expanded to provide more headroom for icons.

## Demon Fang Shops

Demon fang shops use a different data structure than item shops.

### Stock List Hook

**Address**: `main.dll + 0x43F5A0`
**Function**: `onCKibaShop_GetShopStockList`

```cpp
okami::ItemShopStock *__fastcall onCKibaShop_GetShopStockList(okami::cKibaShop *pKibaShop, uint32_t *numItems)
{
    uint32_t mapId = getCurrentMapId();
    if (mapId != 0) {
        okami::ItemShopStock *customStock = ShopRegistry::instance().getDemonFangShopData(mapId, 0, numItems);
        if (customStock != nullptr) {
            return customStock;
        }
    }
    return oCKibaShop_GetShopStockList(pKibaShop, numItems);
}
```

**Difference from other shops**: Demon fang shops query their stock list dynamically via this function, rather than loading from ISL resources. We return a pointer to `thread_local` combined stock data.

### Storage Implementation

```cpp
// In ShopRegistry
std::unordered_map<uint64_t, std::unordered_map<WolfModId, std::vector<okami::ItemShopStock>>> demonFangShops_;

// Shop key combines map ID and shop index
uint64_t makeShopKey(uint32_t mapId, uint32_t shopIdx) {
    return (static_cast<uint64_t>(mapId) << 32) | shopIdx;
}
```

When queried, all mods' items for that shop are combined into a `thread_local std::vector` and its data pointer returned.

### UpdatePurchaseList

**Address**: `main.dll + 0x440380`
**Similar to item shops but**: Icons set to `nullptr` because demon fang shops use the game's built-in icon system (not custom icons yet).

## Skill Shops

**Address**: `main.dll + 0x4431B0`
**Function**: `onCSkillShop_UpdatePurchaseList`

```cpp
for (uint32_t i = 0; i < pShop->numSlots; i++) {
    int32_t skillType = pShop->skillList[i].skillId;

    pShop->shopSlots[i].itemCost = pShop->skillList[i].cost;
    pShop->shopSlots[i].pIcon = nullptr;  // Skills don't use item icons
    pShop->shopSlots[i].itemNameStrId = skillType + 0x2000;  // Skill text offset
}
```

**Text ID offset**: Skill shops use a different MSD section. Skill IDs are offset by 0x2000 to index into `id/idskillshop.idd` compiled MSD data.

**Note**: Skill shop customization is currently not implemented. Hook exists for future extension.

## MSD (Message Data) Integration

MSD files contain all in-game text strings. Custom items/shops require custom strings.

### MSD Pointer Replacement

**Address**: `main.dll + 0x1C9510`
**Hook**: `onLoadCore20MSD`
**Pointer**: `main.dll + 0x9C11B0` (pointer to MSD data)

```cpp
static const void **ppCore20MSD = nullptr;  // Set during setupResourceHooks
static bool msdInitialized = false;

void __fastcall onLoadCore20MSD(void *msgStruct)
{
    oLoadCore20MSD(msgStruct);  // Load original MSD

    if (!ppCore20MSD || !*ppCore20MSD) return;

    if (!msdInitialized) {
        // Read original strings
        wolf::runtime::g_MSDManager.readMSD(*ppCore20MSD);
        msdInitialized = true;
    }

    // CRITICAL: Replace game's pointer
    *ppCore20MSD = wolf::runtime::g_MSDManager.getData();
}
```

**Why pointer replacement**: The game reads `*ppCore20MSD` whenever it needs to display text. By replacing this pointer, all string lookups use our modified MSD data.

**Timing**: This hook fires during game initialization, after MSD is loaded but before the game begins normal operation. This is the only window to replace the pointer.

### MSD Structure

```cpp
#pragma pack(push, 1)
struct MSDHeader {
    uint32_t numEntries;
    uint64_t offsets[1];  // Variable length array
};
#pragma pack(pop)
```

MSD is a simple string table:
- Header contains entry count and offset array
- Each offset points to a uint16_t string
- Strings are terminated by 0x8001 (EndDialog marker)

### String Compilation

```cpp
std::vector<uint16_t> MSDManager::compileString(const std::string &str)
{
    std::vector<uint16_t> result;
    for (char c : str) {
        // ASCII to MSD character code mapping
        if (auto it = ASCIIToMSDMap.find(c); it != ASCIIToMSDMap.end()) {
            result.push_back(it->second);
        } else {
            result.push_back(UnsupportedChar);  // 201
        }
    }
    result.push_back(EndDialog);  // 0x8001
    return result;
}
```

**Character set limitation**: Only ASCII characters with MSD mappings are supported at this time.

## Shop Registry

`ShopRegistry` is a singleton managing all shop customizations.

### Map-to-Shop Resolution

Most maps have exactly one item shop. Seian City Commoners Quarter has two (weapon shop at index 0, fish shop at index 1).

```cpp
const uint8_t *ShopRegistry::getCurrentItemShopData(uint32_t shopNum)
{
    uint32_t currentMapId = getCurrentMapId();

    switch (currentMapId) {
        case MapID::SeianCityCommonersQuarter:
            uint64_t shopKey = makeShopKey(currentMapId, shopNum);
            auto shopIt = itemShops.find(shopKey);
            if (shopIt != itemShops.end())
                return shopIt->second->getData();
            break;

        default:
            // Single shop per map
            uint64_t shopKey = makeShopKey(currentMapId, 0);
            // ... lookup and return
    }
}
```

This function is called from `onLoadRsc` as a fallback when direct (mapId, shopIdx) lookup fails.

### Default Shop Initialization

```cpp
void ShopRegistry::initializeDefaultShops()
{
    // Taka Pass shops (0xF07, 0xF08)
    itemShops[makeShopKey(0xF07, 0)]->setSellValues(DefaultTakaPassItemSellPrices);
    itemShops[makeShopKey(0xF08, 0)]->setSellValues(DefaultTakaPassItemSellPrices);

    // Seian fish shop (0x201, index 1)
    itemShops[makeShopKey(0x201, 1)]->setSellValues(DefaultSeianFishShopItemSellPrices);
}
```

Called during runtime initialization to set up special sell value arrays before any game resources are loaded.

## Memory Structures

### cItemShop (offset from base)

```cpp
struct cItemShop : cShopBase {
    ItemShopStock *itemStockList;     // +0x98: Loaded from ISL
    int32_t *itemSellCosts;           // +0xA0: Sell value array
    int32_t iconRef;                  // +0xA8
    int32_t shopStockVariation;       // +0xAC
    // ... other fields
    uint8_t numItemSlots;             // +0xBF
};
```

**Key fields**:
- `itemStockList`: Points to the ISL item array we provide
- `itemSellCosts`: Points to the sell value array at end of ISL
- `shopSlots`: Array of UI slot data (we write to this in UpdatePurchaseList)

### ShopSlotData

```cpp
struct ShopSlotData {
    int32_t itemType;           // Item ID
    int32_t field_4;
    hx::Texture *pIcon;         // Pointer to icon texture
    int32_t itemCost;           // Purchase price
    int16_t itemNameStrId;      // MSD string index
    int16_t maxCount;           // Max purchasable
    int16_t currentCount;       // Current in inventory
    int16_t field_1A;
    int32_t field_1C;
};
```

This is what the shop UI reads. We populate these fields in UpdatePurchaseList hooks.

## On the Mod side

### Shop Modification

```cpp
bool wolf::addShopItem(uint32_t mapId, uint32_t shop_idx,
                       int32_t itemType, int32_t cost)
```

Add item to shop. Map ID from `okami::MapID` enum, item type from `okami::ItemTypes`.

Example:
```cpp
#include <wolf/wolf.hpp>

WOLF_MOD_INIT() {
    wolf::addShopItem(okami::MapID::KamikiVillage, 0,
                      okami::ItemTypes::HolyBoneL, 500);
}
```

**Timing**: Must be called during `earlyGameInit()`, before game completes initialization.

**Internal flow**:
1. Check `detail::g_runtime` pointer validity
2. Get mod ID via `detail::getCurrentModId()`
3. Call `g_runtime->addShopItem(modId, mapId, shop_idx, itemType, cost)`
4. Runtime forwards to `ShopRegistry::addItemToShop()`
5. Registry creates/updates `ShopDefinition`, marks dirty
6. ISL rebuilt on next access

```cpp
bool wolf::addDemonFangItem(uint32_t mapId, uint32_t shop_idx,
                            int32_t itemType, int32_t cost)
```

Add demon fang shop item. Cost in demon fangs, not yen.

Demon fang shop locations:
- Agata Forest: 0xF03 (cursed), 0xF04 (healed)
- Ark of Yamato: 0x601
- Imperial Palace: 0x701

```cpp
bool wolf::setSellValue(uint32_t mapId, uint32_t shop_idx,
                        int32_t itemType, int32_t sellValue)
```

Override sell value for item type at specific shop. Only affects that shop.

### Callbacks

```cpp
bool wolf::onShopPurchase(std::function<void(ShopType, void*)> callback)
```

Register callback for shop purchase events. Fires **after** game processes purchase (inventory updated, yen deducted).

Example:
```cpp
wolf::onShopPurchase([](wolf::ShopType type, void *shopStruct) {
    if (type == wolf::ShopType::ItemShop) {
        auto *shop = static_cast<okami::cItemShop*>(shopStruct);
        int selIdx = shop->scrollOffset + shop->visualSelectIndex;
        if (selIdx >= 0 && selIdx < shop->numSlots) {
            int itemType = shop->shopSlots[selIdx].itemType;
            wolf::log(wolf::LogLevel::Info, "Purchased item %d", itemType);
        }
    }
});
```

**Callback lifetime**: Framework stores `std::function` in `detail::shop_purchase_callbacks`. Pointer to stored function passed as userdata to C API. Cleanup handler clears callbacks on mod unload.

**Exception handling**: Callbacks wrapped in `noexcept` trampolines with try/catch. Exceptions swallowed to prevent crashes (C API cannot propagate C++ exceptions).

```cpp
bool wolf::onShopInteract(std::function<void(ShopType, void*)> callback)
```

Register callback for shop interaction (browsing, cursor movement). Fires frequently during UI updates. Do not perform heavy operations.

### Icon Replacement

```cpp
bool wolf::replaceShopIcons(const char *customIconPath)
```

Replace shop icon package. Uses resource interception. When game requests `"id/ItemShopBuyIcon.dat"`, runtime redirects to provided path.

Icon package format same as vanilla ItemShopBuyIcon.dat (256x256 texture atlas in resource package format).

**Limitation**: Only one icon package active. Last call wins.

## Usage Patterns

Basic:
```cpp
WOLF_MOD_INIT() {
    wolf::addShopItem(okami::MapID::KamikiVillage, 0,
                      okami::ItemTypes::HolyBoneL, 500);
}
```

Tracking purchases:
```cpp
static std::unordered_map<int, int> purchaseCounts;

WOLF_MOD_INIT() {
    wolf::onShopPurchase([](wolf::ShopType type, void *shopStruct) {
        if (type == wolf::ShopType::ItemShop) {
            auto *shop = static_cast<okami::cItemShop*>(shopStruct);
            int selIdx = shop->scrollOffset + shop->visualSelectIndex;
            int itemType = shop->shopSlots[selIdx].itemType;
            purchaseCounts[itemType]++;
        }
    });
}
```

Custom icons:
```cpp
WOLF_MOD_INIT() {
    wolf::replaceShopIcons("mods/my_mod/CustomIcons.dat");
    wolf::addShopItem(okami::MapID::KamikiVillage, 0,
                      okami::ItemTypes::ArchipelagoTestItem1, 100);
}
```

## Limitations and Known Issues

1. **Shop customization must occur before game init** - No dynamic shop modification during gameplay
2. **Skill shops not yet implemented** - Hooks exist but no API exposed
3. **No validation** - Invalid item types or costs are not checked
4. **ASCII-only MSD strings**
