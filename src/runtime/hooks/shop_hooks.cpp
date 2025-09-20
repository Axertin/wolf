#include "shop_hooks.h"

#include "../core/memory_access.h"
#include "../core/shop_system.h"
#include "../utilities/logger.h"
#include "../utilities/shop_registry.h"
#include "okami/maps.hpp"
#include "okami/structs.hpp"

#include <MinHook.h>

//==============================================================================
// SHOP SYSTEM HOOKS
//==============================================================================

namespace wolf::runtime::hooks
{

// Hook function pointers
static int64_t(__fastcall *oGetShopVariation)(void *, uint32_t shopNum, char **shopTextureName);

// External game functions for icon system
static void *(__fastcall *oLoadRscIdx)(void *pPkg, uint32_t idx);
static hx::Texture *(__fastcall *oCItemShop_GetItemIcon)(okami::cItemShop *pShop, int item);
static const void *(__fastcall *oGetShopMetadata)(void *, uint32_t shopNum, uint32_t *numEvents, char **shopTextureName);
// Helper functions
static uint32_t getCurrentMapId()
{
    // Read current exterior map ID
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

// Item shop hooks
typedef uint32_t(__fastcall *CItemShop_UpdatePurchaseListFn)(okami::cItemShop *pShop);
static CItemShop_UpdatePurchaseListFn oCItemShop_UpdatePurchaseList = nullptr;

static void(__fastcall *oCItemShop_PurchaseItem)(okami::cItemShop *pShop);
static bool(__fastcall *oCItemShop_IsSoldOut)(okami::cItemShop *pShop, uint32_t shopIdx);
static bool(__fastcall *oCItemShop_IsPurchasable)(okami::cItemShop *pShop, uint32_t shopIdx);
static void(__fastcall *oCItemShop_ShopInteractUpdate)(okami::cItemShop *pShop);

// Demon fang shop hooks
typedef uint32_t(__fastcall *CKibaShop_UpdatePurchaseListFn)(okami::cKibaShop *pShop);
static CKibaShop_UpdatePurchaseListFn oCKibaShop_UpdatePurchaseList = nullptr;
static void *(__fastcall *oCKibaShop_GetShopStockList)(okami::cKibaShop *pKibaShop, uint32_t *numItems);
static void(__fastcall *oCKibaShop_PurchaseItem)(okami::cKibaShop *pShop);
static bool(__fastcall *oCKibaShop_IsSoldOut)(okami::cKibaShop *pShop, uint32_t shopIdx);
static bool(__fastcall *oCKibaShop_IsPurchasable)(okami::cKibaShop *pShop, uint32_t shopIdx);
static void(__fastcall *oCKibaShop_ShopInteractUpdate)(okami::cKibaShop *pShop);

// Skill shop hooks
typedef uint32_t(__fastcall *CSkillShop_UpdatePurchaseListFn)(okami::cSkillShop *pShop);
static CSkillShop_UpdatePurchaseListFn oCSkillShop_UpdatePurchaseList = nullptr;
static void(__fastcall *oCSkillShop_PurchaseSkill)(okami::cSkillShop *pShop);
static bool(__fastcall *oCSkillShop_IsSkillNotLearned)(okami::cSkillShop *pShop, uint32_t shopIdx);
static bool(__fastcall *oCSkillShop_IsSoldOut)(okami::cSkillShop *pShop, uint32_t shopIdx);
static bool(__fastcall *oCSkillShop_IsPurchasable)(okami::cSkillShop *pShop, uint32_t shopIdx);
static void(__fastcall *oCSkillShop_ShopInteractUpdate)(okami::cSkillShop *pShop);

// Hook implementations
hx::Texture *__fastcall GetItemIcon(okami::cItemShop *pShop, int item)
{
    if (item == 0 || !pShop->pIconsRsc)
    {
        // Original call still needed as it returns a blank graphic from sgpCore20BinRsc
        return oCItemShop_GetItemIcon(pShop, item);
    }

    // First item in a package is index 1 (following golden implementation)
    return reinterpret_cast<hx::Texture *>(oLoadRscIdx(pShop->pIconsRsc, item + 1));
}

int64_t __fastcall onGetShopVariation(void *unknown, uint32_t shopNum, char **shopTextureName)
{
    // Call the original metadata function but drop most details
    // This prevents variations requiring multiple duplicate shops
    uint32_t numEvents;
    oGetShopMetadata(unknown, shopNum, &numEvents, shopTextureName);
    return 0;
}

// Item shop hook implementations
uint32_t __fastcall onCItemShop_UpdatePurchaseList(okami::cItemShop *pShop)
{
    // Rewrite - don't call original, following the golden implementation
    pShop->numSlots = pShop->numItemSlots;
    constexpr uint32_t MaxVisibleSlots = 4; // From original system
    pShop->numVisibleSlots = std::min(pShop->numSlots, MaxVisibleSlots);

    for (uint32_t i = 0; i < pShop->numSlots; i++)
    {
        int32_t itemType = pShop->itemStockList[i].itemType;
        pShop->shopSlots[i].itemType = itemType;

        // Basic icon and text setup using custom icon system
        pShop->shopSlots[i].pIcon = GetItemIcon(pShop, itemType);
        pShop->shopSlots[i].itemNameStrId = itemType + 294; // Original text ID formula

        // Set purchase parameters
        pShop->shopSlots[i].maxCount = 1;
        pShop->shopSlots[i].currentCount = 0; // TODO: Get from inventory system
        pShop->shopSlots[i].itemCost = pShop->itemStockList[i].cost;
    }
    return pShop->numItemSlots;
}

void __fastcall onCItemShop_PurchaseItem(okami::cItemShop *pShop)
{
    logDebug("[WOLF] Item shop purchase triggered");

    // Extract purchase information before calling original
    int itemType = -1;
    int itemCost = -1;

    if (pShop && pShop->shopSlots)
    {
        int selIdx = pShop->scrollOffset + pShop->visualSelectIndex;
        if (selIdx >= 0 && selIdx < pShop->numSlots)
        {
            itemType = pShop->shopSlots[selIdx].itemType;
            itemCost = pShop->shopSlots[selIdx].itemCost;
        }
    }

    // Call original to perform the purchase
    oCItemShop_PurchaseItem(pShop);

    // Trigger mod callbacks with raw shop struct
    wolf::runtime::internal::callShopPurchase(0, pShop); // shopType=0 (item shop)
}

bool __fastcall onCItemShop_IsSoldOut(okami::cItemShop *pShop, uint32_t shopIdx)
{
    // TODO: Allow mods to override sold out status
    return oCItemShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCItemShop_IsPurchasable(okami::cItemShop *pShop, uint32_t shopIdx)
{
    // TODO: Allow mods to override purchasable status
    return oCItemShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCItemShop_ShopInteractUpdate(okami::cItemShop *pShop)
{
    oCItemShop_ShopInteractUpdate(pShop);

    // Trigger mod callbacks for shop interaction
    wolf::runtime::internal::callShopInteract(0, pShop); // shopType=0 (item shop)
}

// Demon fang shop hook implementations
uint32_t __fastcall onCKibaShop_UpdatePurchaseList(okami::cKibaShop *pShop)
{
    // Rewrite - don't call original, following the golden implementation
    pShop->numSlots = pShop->numShopItems;
    constexpr uint32_t MaxVisibleSlots = 4; // From original system
    pShop->numVisibleSlots = std::min(pShop->numSlots, MaxVisibleSlots);

    for (uint32_t i = 0; i < pShop->numSlots; i++)
    {
        int32_t itemType = pShop->itemStockList[i].itemType;
        pShop->shopSlots[i].itemType = itemType;

        // Basic icon and text setup using custom icon system
        pShop->shopSlots[i].pIcon = GetItemIcon(pShop, itemType);
        pShop->shopSlots[i].itemNameStrId = itemType + 294; // Original text ID formula

        // Set purchase parameters
        pShop->shopSlots[i].maxCount = 1;
        pShop->shopSlots[i].currentCount = 0; // TODO: Get from inventory system
        pShop->shopSlots[i].itemCost = pShop->itemStockList[i].cost;
    }
    return pShop->numShopItems;
}

void *__fastcall onCKibaShop_GetShopStockList(okami::cKibaShop *pKibaShop, uint32_t *numItems)
{
    uint32_t mapId = getCurrentMapId();
    if (mapId != 0)
    {
        okami::ItemShopStock *customStock = ShopRegistry::instance().getDemonFangShopData(mapId, 0, numItems);
        if (customStock != nullptr)
        {
            logDebug("[WOLF] Using custom demon fang shop data for map %u (%u items)", mapId, *numItems);
            return customStock;
        }
    }

    // Fall back to original
    return oCKibaShop_GetShopStockList(pKibaShop, numItems);
}

void __fastcall onCKibaShop_PurchaseItem(okami::cKibaShop *pShop)
{
    logDebug("[WOLF] Demon fang shop purchase triggered");

    // Call original to perform the purchase
    oCKibaShop_PurchaseItem(pShop);

    // Trigger mod callbacks with raw shop struct
    wolf::runtime::internal::callShopPurchase(1, pShop); // shopType=1 (demon fang shop)
}

bool __fastcall onCKibaShop_IsSoldOut(okami::cKibaShop *pShop, uint32_t shopIdx)
{
    return oCKibaShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCKibaShop_IsPurchasable(okami::cKibaShop *pShop, uint32_t shopIdx)
{
    return oCKibaShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCKibaShop_ShopInteractUpdate(okami::cKibaShop *pShop)
{
    oCKibaShop_ShopInteractUpdate(pShop);

    // Trigger mod callbacks for shop interaction
    wolf::runtime::internal::callShopInteract(1, pShop); // shopType=1 (demon fang shop)
}

// Skill shop hook implementations
uint32_t __fastcall onCSkillShop_UpdatePurchaseList(okami::cSkillShop *pShop)
{
    // Rewrite - don't call original, following the golden implementation
    pShop->numSlots = pShop->numSkillSlots;
    constexpr uint32_t MaxVisibleSlots = 4; // From original system
    pShop->numVisibleSlots = std::min(pShop->numSlots, MaxVisibleSlots);

    for (uint32_t i = 0; i < pShop->numSlots; i++)
    {
        int32_t skillType = pShop->skillList[i].skillId;

        // Set cost and basic parameters
        pShop->shopSlots[i].itemCost = pShop->skillList[i].cost;
        pShop->shopSlots[i].pIcon = nullptr; // Skills don't use item icons
        
        // 0x2000 is a context specific offset (from original system)
        // anything 0x2000 + n here is found in id/idskillshop.idd -> bin TBL -> PAC -> MSD
        pShop->shopSlots[i].itemNameStrId = skillType + 0x2000;
    }
    return pShop->numSkillSlots;
}

void __fastcall onCSkillShop_PurchaseSkill(okami::cSkillShop *pShop)
{
    logDebug("[WOLF] Skill shop purchase triggered");

    // Call original to perform the purchase
    oCSkillShop_PurchaseSkill(pShop);

    // Trigger mod callbacks with raw shop struct
    wolf::runtime::internal::callShopPurchase(2, pShop); // shopType=2 (skill shop)
}

bool __fastcall onCSkillShop_IsSkillNotLearned(okami::cSkillShop *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsSkillNotLearned(pShop, shopIdx);
}

bool __fastcall onCSkillShop_IsSoldOut(okami::cSkillShop *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCSkillShop_IsPurchasable(okami::cSkillShop *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCSkillShop_ShopInteractUpdate(okami::cSkillShop *pShop)
{
    oCSkillShop_ShopInteractUpdate(pShop);

    // Trigger mod callbacks for shop interaction
    wolf::runtime::internal::callShopInteract(2, pShop); // shopType=2 (skill shop)
}

bool setupShopHooks(uintptr_t mainBase)
{
    logDebug("[WOLF] Setting up shop hooks...");

    // Shop system hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4420C0), reinterpret_cast<LPVOID>(&onGetShopVariation),
                      reinterpret_cast<LPVOID *>(&oGetShopVariation)) != MH_OK)
        return false;

    // Item shop hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43E250), reinterpret_cast<LPVOID>(&onCItemShop_UpdatePurchaseList),
                      reinterpret_cast<LPVOID *>(&oCItemShop_UpdatePurchaseList)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43CA30), reinterpret_cast<LPVOID>(&onCItemShop_PurchaseItem),
                      reinterpret_cast<LPVOID *>(&oCItemShop_PurchaseItem)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43BAE0), reinterpret_cast<LPVOID>(&onCItemShop_IsSoldOut),
                      reinterpret_cast<LPVOID *>(&oCItemShop_IsSoldOut)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43B6A0), reinterpret_cast<LPVOID>(&onCItemShop_IsPurchasable),
                      reinterpret_cast<LPVOID *>(&oCItemShop_IsPurchasable)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43C6F0), reinterpret_cast<LPVOID>(&onCItemShop_ShopInteractUpdate),
                      reinterpret_cast<LPVOID *>(&oCItemShop_ShopInteractUpdate)) != MH_OK)
        return false;

    // Demon fang shop hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x440380), reinterpret_cast<LPVOID>(&onCKibaShop_UpdatePurchaseList),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_UpdatePurchaseList)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43F5A0), reinterpret_cast<LPVOID>(&onCKibaShop_GetShopStockList),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_GetShopStockList)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43FD30), reinterpret_cast<LPVOID>(&onCKibaShop_PurchaseItem),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_PurchaseItem)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43F440), reinterpret_cast<LPVOID>(&onCKibaShop_IsSoldOut),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_IsSoldOut)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43F2F0), reinterpret_cast<LPVOID>(&onCKibaShop_IsPurchasable),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_IsPurchasable)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43FA90), reinterpret_cast<LPVOID>(&onCKibaShop_ShopInteractUpdate),
                      reinterpret_cast<LPVOID *>(&oCKibaShop_ShopInteractUpdate)) != MH_OK)
        return false;

    // Skill shop hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4431B0), reinterpret_cast<LPVOID>(&onCSkillShop_UpdatePurchaseList),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_UpdatePurchaseList)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x442570), reinterpret_cast<LPVOID>(&onCSkillShop_IsSkillNotLearned),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_IsSkillNotLearned)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x442C40), reinterpret_cast<LPVOID>(&onCSkillShop_PurchaseSkill),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_PurchaseSkill)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4423C0), reinterpret_cast<LPVOID>(&onCSkillShop_IsSoldOut),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_IsSoldOut)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4421C0), reinterpret_cast<LPVOID>(&onCSkillShop_IsPurchasable),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_IsPurchasable)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x442A50), reinterpret_cast<LPVOID>(&onCSkillShop_ShopInteractUpdate),
                      reinterpret_cast<LPVOID *>(&oCSkillShop_ShopInteractUpdate)) != MH_OK)
        return false;

    // Set up external game function addresses for icon system
    oLoadRscIdx = reinterpret_cast<decltype(oLoadRscIdx)>(mainBase + 0x1B16C0);
    oCItemShop_GetItemIcon = reinterpret_cast<decltype(oCItemShop_GetItemIcon)>(mainBase + 0x43BDA0);

    // Hook the GetItemIcon function
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x43BDA0), reinterpret_cast<LPVOID>(&GetItemIcon),
                      reinterpret_cast<LPVOID *>(&oCItemShop_GetItemIcon)) != MH_OK)
        return false;

    return true;
}

} // namespace wolf::runtime::hooks
