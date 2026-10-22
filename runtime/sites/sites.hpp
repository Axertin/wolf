// Sites: named game events that mods subscribe callbacks to, and the functions the game-event detours call.

#pragma once

#include <wolf/sites.h>
#include <wolf/wolf.h>

#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

enum class Site : uint8_t { SessionStart, SessionEnd, Tick, TickPost, RoomChange, ItemAdd };
inline constexpr int kSiteCount = 6;

// Main thread, before any other call here: installs the subscription registry. Later calls return at once.
void sitesInit();

// Any thread. Queues cb( mod, ctx, user ) on the named site for the next frame boundary; registryRemove withdraws it. 0 for a name with no site, or a full
// registry. lifetime is process, session or room; the C table checks it. Callbacks of one site run in load order, then in registration order.
WolfHandle siteOn( WolfMod *mod, const char *name, WolfCallback cb, void *user, int32_t lifetime );

// Game context: runs the site's live callbacks, each as its mod's dispatch, all inside one runtime dispatch scope so the overlap check sees every site event.
void siteDispatch( Site site, void *ctx );

// The game events, each called by its detour on the thread the game runs it on.
void siteSessionStart( bool warm );       // flower_startup return, main; a warm start runs warmStartBoundary first
void siteSessionEnd();                    // SoftReset_ExecuteAndReload entry, main: the session lifetime ends, then session.end runs for the ended session
void siteTickEnter();                     // flower_tick entry, main: frameBoundary, the frame count advances, tick
void siteTickReturn();                    // flower_tick return, main: tick.post with the frame of the tick before it
void siteRoomChangeEnter();               // Room_Change entry, map worker: the room lifetime ends
void siteRoomChangeReturn( uint32_t to ); // Room_Change return, map worker: logs the change, then room.change from the previous room to to
void siteItemAdd( WolfItemAddCtx &ctx );  // before Item_Add runs: item.add; the caller skips the game's function when ctx.handled is nonzero

} // namespace wolf
