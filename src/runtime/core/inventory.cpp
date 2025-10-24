#include "inventory.h"

#include "../hooks/inventory_hooks.h"

extern "C"
{
    void wolfGiveItem(int itemID, int numItems)
    {
        wolf::runtime::hooks::giveItem(itemID, numItems);
    }
}
