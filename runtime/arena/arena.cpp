#include "arena/arena.hpp"

#include <windows.h>

#include <algorithm>

namespace {

constexpr size_t kPageSize = 4096;
constexpr size_t kCommitStep = size_t{ 64 } * 1024; // commits grow in steps of this, so small pushes rarely reach VirtualAlloc

size_t roundUp( size_t value, size_t to )
{
    return ( value + ( to - 1 ) ) & ~( to - 1 );
}

void decommit( uint8_t *at, size_t length )
{
    if ( length != 0 )
    {
        VirtualFree( at, length, MEM_DECOMMIT );
    }
}

} // namespace

namespace wolf {

void *push( Arena &arena, size_t n, size_t align )
{
    if ( n > kMaxPush || align > kMaxPush )
    {
        return nullptr;
    }
    if ( arena.base == nullptr )
    {
        arena.base = static_cast<uint8_t *>( VirtualAlloc( nullptr, arena.reserve, MEM_RESERVE, PAGE_READWRITE ) );
        if ( arena.base == nullptr )
        {
            return nullptr;
        }
    }
    const auto start = reinterpret_cast<uintptr_t>( arena.base );
    const size_t offset = arena.used + ( ( uintptr_t{ 0 } - ( start + arena.used ) ) & ( align - 1 ) );
    if ( offset > arena.reserve || n > arena.reserve - offset )
    {
        arena.full = true;
        return nullptr;
    }
    const size_t end = offset + n;
    if ( end > arena.committed )
    {
        const size_t to = std::min( roundUp( end, kCommitStep ), roundUp( arena.reserve, kPageSize ) );
        if ( VirtualAlloc( arena.base + arena.committed, to - arena.committed, MEM_COMMIT, PAGE_READWRITE ) == nullptr )
        {
            return nullptr;
        }
        arena.committed = to;
    }
    arena.used = end;
    return arena.base + offset;
}

void reset( Arena &arena )
{
    arena.full = false;
    if ( arena.base == nullptr )
    {
        return;
    }
#ifdef NDEBUG
    // Decommitting and recommitting hands back zeroed pages. Only the used part of what is kept needs it; the rest of it was never written.
    const size_t kept = std::min( roundUp( arena.keep, kPageSize ), arena.committed );
    const size_t dirty = std::min( roundUp( arena.used, kPageSize ), kept );
    decommit( arena.base + kept, arena.committed - kept );
    arena.committed = kept;
    if ( dirty != 0 )
    {
        decommit( arena.base, dirty );
        if ( VirtualAlloc( arena.base, dirty, MEM_COMMIT, PAGE_READWRITE ) == nullptr )
        {
            // The pushes after this commit on demand from the base.
            decommit( arena.base, kept );
            arena.committed = 0;
        }
    }
#else
    decommit( arena.base, arena.committed );
    arena.committed = 0;
#endif
    arena.used = 0;
}

} // namespace wolf
