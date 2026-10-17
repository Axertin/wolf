// Growable bump allocators over VirtualAlloc blocks.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wolf {

inline constexpr size_t kArenaGranularity = size_t{ 64 } * 1024; // block sizes are multiples of this
inline constexpr size_t kMaxPush = size_t{ 1 } << 62;            // a larger size or alignment returns null, so no arithmetic below overflows

struct ArenaBlock; // header at the start of each block

// A chain of VirtualAlloc blocks. The first push maps the first block. Allocations never move. blockSize is the only member a definition sets. One thread at a
// time: the caller serialises push and reset (the runtime holds the runtime lock, or owns an arena nothing else can reach).
struct Arena
{
    size_t blockSize = 0;
    ArenaBlock *current = nullptr; // block taking new allocations
    ArenaBlock *first = nullptr;   // oldest block, the one a reset keeps
    ArenaBlock *large = nullptr;   // dedicated blocks for allocations larger than a block
};

// Zeroed memory aligned to align (a power of two). Null when n or align exceeds kMaxPush, or a block cannot be mapped or a page committed.
void *push( Arena &arena, size_t n, size_t align );

// Releases every block but the first and re-zeroes the first block's used range.
void reset( Arena &arena );

} // namespace wolf
