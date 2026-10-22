// Process, session, room and frame lifetimes on top of arenas.

#pragma once

#include <wolf/wolf.h>

#include <cstddef>
#include <cstdint>

#include "arena/arena.hpp"
#include "mods/mod.hpp"

namespace wolf {

// Zeroed memory that lives until the lifetime's current generation ends. Null when the commit fails or the push would pass the arena's reservation, each
// logging once per mod. Any thread, every lifetime; takes the runtime lock. A push that takes an arena past the lifetime's highest size so far (16 MiB, then
// each doubling) warns naming mod. lifetime is a WOLF_LIFETIME_* value and align a power of two; the C table checks both.
void *lifetimeAlloc( WolfMod *mod, int32_t lifetime, size_t n, size_t align );

// Runs fn(mod, data), as mod's dispatch, when the lifetime's current generation is cleaned up, newest first. 1 when registered; 0 for a failed allocation,
// which logs like lifetimeAlloc. fn is not null; the C table checks it. Process-lifetime finalizers never run. Takes the runtime lock.
int32_t lifetimeOnReset( WolfMod *mod, int32_t lifetime, WolfFinalizer fn, void *data );

uint32_t lifetimeGeneration( int32_t lifetime ); // lock-free, any thread

// Ends the current generation of WOLF_LIFETIME_SESSION (which also ends the room lifetime) or WOLF_LIFETIME_ROOM, on any thread; takes the runtime lock. Each
// of these lifetimes has three arenas: new allocations go to the next one from here on, and the ended one stays intact until lifetimeCleanup.
void lifetimeEnd( int32_t lifetime );

// A lifetime's arenas that hold an ended generation waiting for lifetimeCleanup.
struct WaitingArenas
{
    int32_t lifetime = 0;
    uint32_t mask = 0;   // bit i stands for the lifetime's arena i
    uint32_t newest = 0; // the arena that ended last, when mask is not 0
};

// Main thread, frame boundary, unlocked: tears down each arena in waiting.mask, newest ended first, running its finalizers newest first and resetting it. Then
// one short hold of the runtime lock clears their waiting bits.
void lifetimeCleanup( WaitingArenas waiting );

// The lifetime's waiting arenas. The caller holds the runtime lock; one hold sees session and room as one lifetimeEnd left them. Their waiting bits stay set
// until lifetimeCleanup's last hold, so a lifetime end in between merges instead of reusing an arena being cleaned.
WaitingArenas lifetimeWaiting( int32_t lifetime );

// Main thread, frame boundary: one hold of the runtime lock makes the frame lifetime's other arena current and advances the frame generation; then, unlocked,
// the ended frame's finalizers run newest first and its arena resets. What those finalizers push or register lands in the next frame.
void lifetimeResetFrame();

Arena &processArena(); // shared with lifetimeAlloc( WOLF_LIFETIME_PROCESS ), so the caller holds the runtime lock while it pushes

const char *lifetimeName( int32_t lifetime ); // "process", "session", "room", "frame"

} // namespace wolf
