#include "shop_hooks.h"

#include "../utilities/logger.h"

#include <MinHook.h>

//==============================================================================
// SHOP SYSTEM HOOKS
//==============================================================================

namespace wolf::runtime::hooks
{

// Hook function pointers
static int64_t(__fastcall *oGetShopVariation)(void *, uint32_t shopNum, char **shopTextureName);
static const void *(__fastcall *oGetShopMetadata)(void *, uint32_t shopNum, uint32_t *numEvents, char **shopTextureName);

// Item shop hooks
typedef uint32_t(__fastcall *CItemShop_UpdatePurchaseListFn)(void *pShop);
static CItemShop_UpdatePurchaseListFn oCItemShop_UpdatePurchaseList = nullptr;

static void(__fastcall *oCItemShop_PurchaseItem)(void *pShop);
static bool(__fastcall *oCItemShop_IsSoldOut)(void *pShop, uint32_t shopIdx);
static bool(__fastcall *oCItemShop_IsPurchasable)(void *pShop, uint32_t shopIdx);
static void(__fastcall *oCItemShop_ShopInteractUpdate)(void *pShop);

// Demon fang shop hooks
static void *(__fastcall *oCKibaShop_GetShopStockList)(void *pKibaShop, uint32_t *numItems);
static void(__fastcall *oCKibaShop_PurchaseItem)(void *pShop);
static bool(__fastcall *oCKibaShop_IsSoldOut)(void *pShop, uint32_t shopIdx);
static bool(__fastcall *oCKibaShop_IsPurchasable)(void *pShop, uint32_t shopIdx);
static void(__fastcall *oCKibaShop_ShopInteractUpdate)(void *pShop);

// Skill shop hooks
static void(__fastcall *oCSkillShop_PurchaseSkill)(void *pShop);
static bool(__fastcall *oCSkillShop_IsSkillNotLearned)(void *pShop, uint32_t shopIdx);
static bool(__fastcall *oCSkillShop_IsSoldOut)(void *pShop, uint32_t shopIdx);
static bool(__fastcall *oCSkillShop_IsPurchasable)(void *pShop, uint32_t shopIdx);
static void(__fastcall *oCSkillShop_ShopInteractUpdate)(void *pShop);

// Hook implementations
int64_t __fastcall onGetShopVariation(void *unknown, uint32_t shopNum, char **shopTextureName)
{
    // Call the original metadata function but drop most details
    // This prevents variations requiring multiple duplicate shops
    uint32_t numEvents;
    oGetShopMetadata(unknown, shopNum, &numEvents, shopTextureName);
    return 0;
}

// Item shop hook implementations
void __fastcall onCItemShop_PurchaseItem(void *pShop)
{
    logDebug("[WOLF] Item shop purchase triggered");

    // TODO: Add mod callback system for shop purchases
    // For now, just call original
    oCItemShop_PurchaseItem(pShop);
}

bool __fastcall onCItemShop_IsSoldOut(void *pShop, uint32_t shopIdx)
{
    // TODO: Allow mods to override sold out status
    return oCItemShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCItemShop_IsPurchasable(void *pShop, uint32_t shopIdx)
{
    // TODO: Allow mods to override purchasable status
    return oCItemShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCItemShop_ShopInteractUpdate(void *pShop)
{
    oCItemShop_ShopInteractUpdate(pShop);
    // TODO: Allow mods to modify shop descriptions
}

// Demon fang shop hook implementations
void *__fastcall onCKibaShop_GetShopStockList(void *pKibaShop, uint32_t *numItems)
{
    // TODO: Allow mods to provide custom shop stock
    return oCKibaShop_GetShopStockList(pKibaShop, numItems);
}

void __fastcall onCKibaShop_PurchaseItem(void *pShop)
{
    logDebug("[WOLF] Demon fang shop purchase triggered");
    oCKibaShop_PurchaseItem(pShop);
}

bool __fastcall onCKibaShop_IsSoldOut(void *pShop, uint32_t shopIdx)
{
    return oCKibaShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCKibaShop_IsPurchasable(void *pShop, uint32_t shopIdx)
{
    return oCKibaShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCKibaShop_ShopInteractUpdate(void *pShop)
{
    oCKibaShop_ShopInteractUpdate(pShop);
}

// Skill shop hook implementations
void __fastcall onCSkillShop_PurchaseSkill(void *pShop)
{
    logDebug("[WOLF] Skill shop purchase triggered");
    oCSkillShop_PurchaseSkill(pShop);
}

bool __fastcall onCSkillShop_IsSkillNotLearned(void *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsSkillNotLearned(pShop, shopIdx);
}

bool __fastcall onCSkillShop_IsSoldOut(void *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsSoldOut(pShop, shopIdx);
}

bool __fastcall onCSkillShop_IsPurchasable(void *pShop, uint32_t shopIdx)
{
    return oCSkillShop_IsPurchasable(pShop, shopIdx);
}

void __fastcall onCSkillShop_ShopInteractUpdate(void *pShop)
{
    oCSkillShop_ShopInteractUpdate(pShop);
}

bool setupShopHooks(uintptr_t mainBase)
{
    logDebug("[WOLF] Setting up shop hooks...");

    // Shop system hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4420C0), reinterpret_cast<LPVOID>(&onGetShopVariation),
                      reinterpret_cast<LPVOID *>(&oGetShopVariation)) != MH_OK)
        return false;

    // Item shop hooks
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

    return true;
}

} // namespace wolf::runtime::hooks
