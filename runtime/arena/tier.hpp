// Process, session, room and frame lifetimes on top of arenas.

#pragma once

#include <wolf/wolf.h>

#include <cstddef>
#include <cstdint>

#include "arena/arena.hpp"
#include "mods/mod.hpp"

namespace wolf {

// Zeroed memory that lives until the tier's current generation ends. Null on a failed block map or commit, which logs once per mod. Any thread, every tier;
// takes the runtime lock. A push that takes an arena past the tier's highest size so far (16 MiB, then each doubling) warns naming mod. tier is a WOLF_TIER_*
// value and align a power of two; the C table checks both.
void *tierAlloc( WolfMod *mod, int32_t tier, size_t n, size_t align );

// Runs fn(mod, data), as mod's dispatch, when the tier's current generation is cleaned up, newest first. 1 when registered; 0 for a failed allocation, which
// logs like tierAlloc. fn is not null; the C table checks it. Process-tier finalizers never run. Takes the runtime lock.
int32_t tierOnReset( WolfMod *mod, int32_t tier, WolfFinalizer fn, void *data );

uint32_t tierGeneration( int32_t tier ); // lock-free, any thread

// Ends the current generation of WOLF_TIER_SESSION (which also ends the room tier) or WOLF_TIER_ROOM, on any thread; takes the runtime lock. Each of these
// tiers has three arenas: new allocations go to the next one from here on, and the ended one stays intact until tierCleanup.
void tierEnd( int32_t tier );

// A tier's arenas that hold an ended generation waiting for tierCleanup.
struct WaitingArenas
{
    int32_t tier = 0;
    uint32_t mask = 0;   // bit i stands for the tier's arena i
    uint32_t newest = 0; // the arena that ended last, when mask is not 0
};

// Main thread, frame boundary, unlocked: tears down each arena in waiting.mask, newest ended first, running its finalizers newest first and resetting it. Then
// one short hold of the runtime lock clears their waiting bits.
void tierCleanup( WaitingArenas waiting );

// The tier's waiting arenas. The caller holds the runtime lock; one hold sees session and room as one tierEnd left them. Their waiting bits stay set until
// tierCleanup's last hold, so a tier end in between merges instead of reusing an arena being cleaned.
WaitingArenas tierWaiting( int32_t tier );

// Main thread, frame boundary: one hold of the runtime lock makes the frame tier's other arena current and advances the frame generation; then, unlocked, the
// ended frame's finalizers run newest first and its arena resets. What those finalizers push or register lands in the next frame.
void tierResetFrame();

Arena &processArena(); // shared with tierAlloc( WOLF_TIER_PROCESS ), so the caller holds the runtime lock while it pushes

const char *tierName( int32_t tier ); // "process", "session", "room", "frame"

} // namespace wolf
