#include <windows.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <arena/arena.hpp>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr size_t KiB = 1024;

DWORD state( const void *p )
{
    MEMORY_BASIC_INFORMATION info = {};
    VirtualQuery( p, &info, sizeof( info ) );
    return info.State;
}

// The start of the reservation p lies in.
const void *reservationOf( const void *p )
{
    MEMORY_BASIC_INFORMATION info = {};
    VirtualQuery( p, &info, sizeof( info ) );
    return info.AllocationBase;
}

} // namespace

TEST_SUITE( "arena" )
{
    TEST_CASE( "allocations come back zeroed, aligned and in order" )
    {
        wolf::Arena arena{ .reserve = 1024 * KiB, .keep = 64 * KiB };
        auto *a = static_cast<unsigned char *>( wolf::push( arena, 100, 16 ) );
        auto *b = static_cast<unsigned char *>( wolf::push( arena, 8, 64 ) );
        REQUIRE( a != nullptr );
        REQUIRE( b != nullptr );
        CHECK( reinterpret_cast<uintptr_t>( a ) % 16 == 0 );
        CHECK( reinterpret_cast<uintptr_t>( b ) % 64 == 0 );
        CHECK( b > a );
        CHECK( std::ranges::all_of( std::span( a, 100 ), []( unsigned char c ) { return c == 0; } ) );
        wolf::reset( arena );
    }

    TEST_CASE( "pushes stay contiguous across commit steps and earlier memory stays put" )
    {
        wolf::Arena arena{ .reserve = 4096 * KiB, .keep = 64 * KiB };
        auto *first = static_cast<unsigned char *>( wolf::push( arena, 40 * KiB, 1 ) );
        REQUIRE( first != nullptr );
        std::memset( first, 0xAB, 40 * KiB );
        const unsigned char *expected = first + ( 40 * KiB );
        for ( int i = 0; i < 64; ++i )
        {
            auto *next = static_cast<unsigned char *>( wolf::push( arena, 40 * KiB, 1 ) );
            REQUIRE( next == expected );
            CHECK( next[ ( 40 * KiB ) - 1 ] == 0 );
            expected += 40 * KiB;
        }
        CHECK( first[ 0 ] == 0xAB );
        CHECK( first[ ( 40 * KiB ) - 1 ] == 0xAB );
        wolf::reset( arena );
    }

    TEST_CASE( "memory is committed only as pushes reach it" )
    {
        wolf::Arena arena{ .reserve = 1024 * KiB, .keep = 64 * KiB };
        auto *p = static_cast<unsigned char *>( wolf::push( arena, 16, 16 ) );
        REQUIRE( p != nullptr );
        CHECK( state( p ) == MEM_COMMIT );
        CHECK( state( p + ( 512 * KiB ) ) == MEM_RESERVE );
        auto *big = static_cast<unsigned char *>( wolf::push( arena, 600 * KiB, 16 ) );
        REQUIRE( big != nullptr );
        CHECK( state( p + ( 512 * KiB ) ) == MEM_COMMIT );
        wolf::reset( arena );
    }

    TEST_CASE( "a push past the reservation returns null, marks the arena full and maps nothing new" )
    {
        wolf::Arena arena{ .reserve = 256 * KiB, .keep = 64 * KiB };
        auto *a = static_cast<unsigned char *>( wolf::push( arena, 200 * KiB, 1 ) );
        REQUIRE( a != nullptr );
        CHECK_FALSE( arena.full );
        CHECK( wolf::push( arena, 100 * KiB, 1 ) == nullptr );
        CHECK( arena.full );
        // The reservation ends at 256 KiB; whatever lies past it belongs to someone else.
        CHECK( reservationOf( a + ( 256 * KiB ) - 1 ) == reservationOf( a ) );
        CHECK( reservationOf( a + ( 256 * KiB ) ) != reservationOf( a ) );
        // What still fits keeps coming from the same range.
        CHECK( wolf::push( arena, 16, 1 ) == a + ( 200 * KiB ) );
        wolf::reset( arena );
        CHECK_FALSE( arena.full );
    }

    TEST_CASE( "a size or alignment above kMaxPush returns null and maps nothing" )
    {
        wolf::Arena arena{ .reserve = 1024 * KiB, .keep = 64 * KiB };
        auto *before = static_cast<unsigned char *>( wolf::push( arena, 16, 16 ) );
        REQUIRE( before != nullptr );
        CHECK( wolf::push( arena, SIZE_MAX, 1 ) == nullptr );
        CHECK( wolf::push( arena, 16, size_t{ 1 } << 63 ) == nullptr );
        auto *after = static_cast<unsigned char *>( wolf::push( arena, 16, 16 ) );
        CHECK( after == before + 16 );
        wolf::reset( arena );
    }

    TEST_CASE( "a reservation the system refuses returns null without marking the arena full" )
    {
        wolf::Arena arena{ .reserve = size_t{ 1 } << 62, .keep = 64 * KiB };
        CHECK( wolf::push( arena, 16, 16 ) == nullptr );
        CHECK_FALSE( arena.full );
        wolf::reset( arena );
    }

    TEST_CASE( "reset re-zeroes what was used, keeps up to keep committed in release builds and starts again at the base" )
    {
        wolf::Arena arena{ .reserve = 1024 * KiB, .keep = 64 * KiB };
        auto *head = static_cast<unsigned char *>( wolf::push( arena, 32 * KiB, 4096 ) );
        auto *beyond = static_cast<unsigned char *>( wolf::push( arena, 200 * KiB, 4096 ) );
        REQUIRE( head != nullptr );
        REQUIRE( beyond != nullptr );
        std::memset( head, 0xCD, 32 * KiB );
        std::memset( beyond, 0xCD, 200 * KiB );
        wolf::reset( arena );

        CHECK( state( beyond + ( 100 * KiB ) ) == MEM_RESERVE );
#ifdef NDEBUG
        CHECK( state( head ) == MEM_COMMIT );
        CHECK( head[ 0 ] == 0 );
        CHECK( head[ ( 32 * KiB ) - 1 ] == 0 );
#else
        CHECK( state( head ) == MEM_RESERVE ); // a stale pointer faults at its use
#endif
        auto *again = static_cast<unsigned char *>( wolf::push( arena, 32 * KiB, 4096 ) );
        CHECK( again == head );
        CHECK( again[ ( 32 * KiB ) - 1 ] == 0 );
        auto *later = static_cast<unsigned char *>( wolf::push( arena, 200 * KiB, 4096 ) );
        CHECK( later == beyond );
        CHECK( later[ 0 ] == 0 );
        wolf::reset( arena );
    }
}
