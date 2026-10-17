// The runtime's per-mod record.

#pragma once

#include <wolf/wolf.h>

#include <atomic>
#include <cstdint>

namespace wolf {

inline constexpr int32_t kTierCount = 4;

// Bits of WolfMod::loggedOnce, one per line a mod triggers at most once.
enum LogOnce : uint8_t {
    kOnceOutOfMemory = 0,
    kOnceRunQueueFull = 1,
    kOncePendingFull = 2,
    kOnceRegistryFull = 8, // plus the registry's kind
};

} // namespace wolf

struct WolfMod
{
    std::atomic<uint32_t> loggedOnce{ 0 };
};

namespace wolf {

// True the first time it is called for this mod and bit.
inline bool logOnce( WolfMod *mod, uint32_t bit )
{
    const uint32_t mask = 1U << bit;
    return ( mod->loggedOnce.fetch_or( mask ) & mask ) == 0;
}

} // namespace wolf
