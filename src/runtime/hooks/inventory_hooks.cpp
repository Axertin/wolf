#include "inventory_hooks.h"

#include "../core/game_hooks.h"
#include "../core/memory_access.h"
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

        // Call blocking callbacks (which also call non-blocking ones first)
        bool blocked = wolf::runtime::internal::callItemPickupBlocking(itemId, numItems);

        // If blocked, don't call the original function
        if (blocked)
        {
            logDebug("[WOLF] Item pickup blocked by mod callback");
            return;
        }
    }

    oItemPickup(inventoryStruct, itemId, numItems);
}

bool __fastcall onBrushEdit(void *inventoryStruct, int bitIndex, int operation)
{
    logDebug("[WOLF] Brush edit: bitIndex=0x%X, operation=0x%X", bitIndex, operation);

    // Call brush edit callbacks
    bool blocked = wolf::runtime::internal::callBrushEdit(bitIndex, operation);

    // If blocked, don't call the original function
    if (blocked)
    {
        logDebug("[WOLF] Brush edit blocked by mod callback");
        return false; // Return false to indicate operation failed/was blocked
    }

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
