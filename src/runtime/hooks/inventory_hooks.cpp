#include "inventory_hooks.h"

#include "../utilities/logger.h"
#include "../wolf_runtime_api.h"

#include <MinHook.h>

//==============================================================================
// INVENTORY & ITEM HOOKS
//==============================================================================

namespace wolf::runtime::hooks
{

// Hook function pointers
typedef void(__fastcall *ItemPickupFn)(void *inventoryStruct, int itemId, int numItems);
static ItemPickupFn oItemPickup = nullptr;

typedef bool(__fastcall *EditBrushesFn)(void *inventoryStruct, int bitIndex, int operation);
static EditBrushesFn oEditBrushes = nullptr;

// Hook implementations
void __fastcall onItemPickup(void *inventoryStruct, int itemId, int numItems)
{
    if (numItems > 0)
    {
        logDebug("[WOLF] Item pickup: ID=%d, quantity=%d", itemId, numItems);
        wolf::runtime::internal::callItemPickup(itemId, numItems);
    }

    oItemPickup(inventoryStruct, itemId, numItems);
}

bool __fastcall onBrushEdit(void *inventoryStruct, int bitIndex, int operation)
{
    logDebug("[WOLF] Brush edit: bitIndex=0x%X, operation=0x%X", bitIndex, operation);

    // For now, just call the original - mods can override this behavior
    return oEditBrushes(inventoryStruct, bitIndex, operation);
}

bool setupInventoryHooks(uintptr_t mainBase)
{
    logInfo("[WOLF] Setting up inventory hooks...");

    // Item and inventory hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4965D0), reinterpret_cast<LPVOID>(&onItemPickup), reinterpret_cast<LPVOID *>(&oItemPickup)) !=
        MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x17C270), reinterpret_cast<LPVOID>(&onBrushEdit), reinterpret_cast<LPVOID *>(&oEditBrushes)) !=
        MH_OK)
        return false;

    logInfo("[WOLF] Inventory hooks setup complete");
    return true;
}

void giveItem(int itemId, int numItems)
{
    if (oItemPickup)
    {
        logDebug("[WOLF] Giving %d of item 0x%X", numItems, itemId);
        static uintptr_t mainBase = wolfRuntimeGetModuleBase("main.dll");
        oItemPickup(reinterpret_cast<void *>(mainBase + 0xB66670), itemId, numItems);
    }
}

} // namespace wolf::runtime::hooks
