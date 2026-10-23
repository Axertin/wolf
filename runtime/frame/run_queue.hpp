// Calls queued from any thread, with a small copied payload, to run later in game context.

#pragma once

#include <wolf/wolf.h>

#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

inline constexpr uint32_t kRunMaxPayload = 64;

// Copies the payload into the queue buffer that is taking calls. Any thread. 1 when queued; 0 when the queue cannot grow, which logs once per mod. mod and
// fn are not null, len is at most 64 and data is not null when len is nonzero; the C table checks them.
int32_t runInGame( WolfMod *mod, WolfRunFn fn, const void *data, uint32_t len );

// Main thread, the run step: swaps the buffers and runs the swapped-out one in call order, each call as its mod's dispatch, skipping refused mods' calls. fn
// gets the queued copy, aligned to 16 and valid for the call. Calls made while it runs land in the other buffer.
void runQueued();

} // namespace wolf
