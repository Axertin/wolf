#include "inventory.h"

#include "../hooks/inventory_hooks.h"

void __cdecl wolfGiveItem(int itemID, int numItems)
{
    wolf::runtime::hooks::giveItem(itemID, numItems);
}
