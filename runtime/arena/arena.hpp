// Bump allocators over one contiguous reservation each.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wolf {

inline constexpr size_t kMaxPush = size_t{ 1 } << 62; // a larger size or alignment returns null, so no arithmetic below overflows

// One contiguous range of address space, reserved on the first push and never grown past, with physical memory committed only as pushes reach it.
// Allocations never move, and every allocation of an arena lies inside its one range. reserve and keep are the members a definition sets. One thread at a
// time: the caller serialises push and reset (the runtime holds the runtime lock, or owns an arena nothing else can reach).
struct Arena
{
    size_t reserve = 0;      // bytes of address space; a push that would pass it fails and sets full
    size_t keep = 0;         // committed bytes a reset keeps in release builds
    uint8_t *base = nullptr; // start of the reservation, null until the first push
    size_t used = 0;         // offset of the first free byte
    size_t committed = 0;    // offset where committed memory ends
    bool full = false;       // a push failed because it would pass the reservation; cleared by reset
};

// Zeroed memory aligned to align (a power of two). Null when n or align exceeds kMaxPush, the push would pass the reservation (which sets full), or the
// reservation or a commit fails.
void *push( Arena &arena, size_t n, size_t align );

// Forgets every allocation and zeroes the memory for the next pushes. Release builds keep up to keep bytes committed and decommit the rest; debug builds
// decommit everything, so a pointer kept past the reset faults where it is used.
void reset( Arena &arena );

} // namespace wolf
