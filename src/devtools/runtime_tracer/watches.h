#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wolf_tracer
{

struct Watch
{
    uintptr_t offsetFromMain; // main.dll-relative
    uint32_t mask;
    std::string label;
};

// Pure: returns true iff (oldVal ^ newVal) & mask is non-zero.
inline bool diffMaskedValue(uint32_t oldVal, uint32_t newVal, uint32_t mask) noexcept
{
    return ((oldVal ^ newVal) & mask) != 0;
}

// Append the curated default watch set (state-bit setter outputs, area-load
// flags, controls-disabled bit, etc.) to the live watch list. Idempotent —
// safe to call multiple times. Not invoked at boot; users opt in via the
// `watch defaults` console command.
void seedDefaultWatches();

// Mutators for the runtime watch set.
void addWatch(uintptr_t offsetFromMain, uint32_t mask, std::string label);
bool removeWatch(uintptr_t offsetFromMain);
// Not thread-safe; intended for single-threaded use (game thread / console
// handler / ImGui overlay all run on the same thread). Do not call concurrently
// with addWatch/removeWatch/clearWatches/loadWatches.
const std::vector<Watch> &listWatches();
void clearWatches();

// Run one diff pass: reads current values, compares to last snapshot, emits
// WatchChange events for any masked changes, then updates the snapshot.
void tickWatches();

// JSON load/save of the watch list. Returns true on success.
bool saveWatches(const std::string &path);
bool loadWatches(const std::string &path);

} // namespace wolf_tracer
