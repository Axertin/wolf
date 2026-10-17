// Entry tables that grow in chunks and never move their entries.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

inline constexpr uint32_t kChunkShift = 12;
inline constexpr uint32_t kChunkEntries = 1U << kChunkShift; // 4096
inline constexpr uint32_t kMaxChunks = 256;
inline constexpr uint32_t kMaxEntries = kChunkEntries * kMaxChunks; // 2^20, the handle limit

// Entries in chunks of 4096 that are allocated from the process arena the first time an index reaches them and never move. Index 0 is never handed out.
// Lock-free readers stop at tableEnd, which is published after the chunk exists.
struct Table
{
    const char *name = nullptr; // for log lines
    uint32_t stride = 0;        // bytes per entry
    uint32_t expected = 0;      // entry count past which the size warnings start; 0 for none
    uint32_t onceBit = 0;       // LogOnce bit for the "is full" line
    std::atomic<uint32_t> count{ 0 };
    uint32_t chunkCount = 0;
    uint32_t nextWarn = 0; // see warnPast
    uint8_t *chunks[ kMaxChunks ] = {};
};

// Logs "<name> table passed <threshold> entries" naming mod when entries exceeds the threshold (nextWarn, or expected while nextWarn is 0), then doubles the
// threshold. Does nothing when expected is 0. Caller holds the runtime lock. Tables call it with their entry count, registries with their used count.
void warnPast( WolfMod *mod, const char *name, uint32_t expected, uint32_t &nextWarn, uint32_t entries );

// A zeroed entry and its index. The caller holds the runtime lock. Null when the table holds kMaxEntries or a chunk cannot be allocated; that logs once per mod
// and table.
void *tableAppend( Table &table, WolfMod *mod, uint32_t *index );

// The entry at index, which must be below tableEnd.
inline void *tableAt( Table &table, uint32_t index )
{
    return table.chunks[ index >> kChunkShift ] + ( static_cast<size_t>( index & ( kChunkEntries - 1 ) ) * table.stride );
}

// One past the last published index.
inline uint32_t tableEnd( const Table &table )
{
    return table.count.load( std::memory_order_acquire );
}

// Forgets every entry; chunks stay allocated for the next appends. The caller guarantees that no append runs concurrently and no reader is still using an
// entry.
void tableClear( Table &table );

} // namespace wolf
