// The runtime's per-mod record.

#pragma once

#include <wolf/wolf.h>

#include <atomic>
#include <cstdint>

namespace wolf {

inline constexpr int32_t kLifetimeCount = 4;

// Bits of WolfMod::loggedOnce, one per line a mod triggers at most once.
enum LogOnce : uint8_t {
    kOnceOutOfMemory = 0,
    kOnceRunQueueFull = 1,
    kOncePendingFull = 2,
    kOnceHookTargetsFull = 3,
    kOnceHookLinksFull = 4,
    kOnceArenaFull = 5,
    kOnceRegistryFull = 8, // plus the registry's kind
};

} // namespace wolf

struct WolfMod
{
    std::atomic<uint32_t> loggedOnce{ 0 };
    uint32_t loadIndex = 0; // position in load order; a later mod's hooks sit further out and its callbacks run later
};

namespace wolf {

// True the first time it is called for this mod and bit, and always for a null mod: the runtime's own lines have no mod to remember them on.
inline bool logOnce( WolfMod *mod, uint32_t bit )
{
    if ( mod == nullptr )
    {
        return true;
    }
    const uint32_t mask = 1U << bit;
    return ( mod->loggedOnce.fetch_or( mask ) & mask ) == 0;
}

} // namespace wolf
