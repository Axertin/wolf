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
    kOnceFlagsNotReady = 6,
    kOnceConfigFull = 7,
    kOnceModsFull = 8,
    kOnceRegistryFull = 16, // plus the registry's kind, 16..31
    kOnceEntry = 32,        // plus the WolfApi entry's ordinal, 0 (log) through 24 (claimId); 32..56
};

} // namespace wolf

struct WolfMod
{
    std::atomic<uint64_t> loggedOnce{ 0 };
    std::atomic<bool> refused{ false }; // set on the main thread at boot, read from any thread
    uint32_t loadIndex = 0;             // position in load order; a later mod's hooks sit further out and its callbacks run later
    int32_t priority = 0;
    const char *id = "";              // directory name under mods/, UTF-8
    const char *name = "";            // mod.ini name, or the id
    const char *version = "";         // mod.ini version, or empty
    const char *path = "";            // absolute UTF-8 directory with a trailing backslash; what modPath returns
    const wchar_t *dllPath = nullptr; // <dir>\<id>.dll when it exists; null for an asset-only mod
    uint32_t firstConfig = 0;         // config table index of the mod's first entry
    uint32_t configCount = 0;
};

namespace wolf {

// True the first time it is called for this mod and bit, and always for a null mod: the runtime's own lines have no mod to remember them on.
inline bool logOnce( WolfMod *mod, uint32_t bit )
{
    if ( mod == nullptr )
    {
        return true;
    }
    const uint64_t mask = uint64_t{ 1 } << bit;
    return ( mod->loggedOnce.fetch_or( mask ) & mask ) == 0;
}

} // namespace wolf
