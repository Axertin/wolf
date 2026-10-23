// The game's own state that mods reach through the C table: progress flags and items, changed through the game's functions.

#pragma once

#include <wolf/wolf.h>

#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

// Any thread. group is below 80 and bit below 256, which the C table checks. Until the game sets g_progressFlagBlockPtr, set and clear do nothing, test
// returns 0, and each logs "flags are not ready until the game has started; <entry> ignored" once per mod.
void flagSet( WolfMod *mod, uint32_t group, uint32_t bit );     // ProgressFlag_Set( group << 16 | bit )
void flagClear( WolfMod *mod, uint32_t group, uint32_t bit );   // ProgressFlag_Clear( group << 16 | bit )
int32_t flagTest( WolfMod *mod, uint32_t group, uint32_t bit ); // 1 or 0, read from the block

// Any thread. itemId is 0 to 255, which the C table checks. On a thread inside a dispatch once the game has started, calls Item_Add's address with
// *g_ItemMgrPtr, so the item.add site and every hook see the call, with itemRequester() returning mod for its length. Otherwise queues the same call through
// runInGame. 1 when it ran or was queued, 0 when the queue cannot grow.
int32_t giveItem( WolfMod *mod, int32_t itemId, int32_t delta );

WolfMod *itemRequester(); // the mod whose giveItem this thread is running, or null

} // namespace wolf
