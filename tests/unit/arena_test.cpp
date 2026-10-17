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

} // namespace

TEST_SUITE( "arena" )
{
    TEST_CASE( "allocations come back zeroed, aligned and in order within a block" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
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

    TEST_CASE( "an allocation that does not fit adds a block and earlier memory stays put" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
        auto *p1 = static_cast<unsigned char *>( wolf::push( arena, 40 * KiB, 16 ) );
        std::memset( p1, 0xAB, 40 * KiB );
        auto *p2 = static_cast<unsigned char *>( wolf::push( arena, 40 * KiB, 16 ) );
        REQUIRE( p2 != nullptr );
        CHECK( ( p2 < p1 || p2 >= p1 + ( 64 * KiB ) ) );
        CHECK( p1[ 0 ] == 0xAB );
        CHECK( p1[ ( 40 * KiB ) - 1 ] == 0xAB );
        wolf::reset( arena );
    }

    TEST_CASE( "an allocation larger than a block gets a dedicated block and the current block keeps filling" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
        auto *small1 = static_cast<unsigned char *>( wolf::push( arena, 64, 16 ) );
        auto *big = static_cast<unsigned char *>( wolf::push( arena, 256 * KiB, 16 ) );
        auto *small2 = static_cast<unsigned char *>( wolf::push( arena, 64, 16 ) );
        REQUIRE( big != nullptr );
        CHECK( small2 == small1 + 64 );
        CHECK( big[ 0 ] == 0 );
        CHECK( big[ ( 256 * KiB ) - 1 ] == 0 );
        wolf::reset( arena );
    }

    TEST_CASE( "a size or alignment above kMaxPush returns null and maps nothing" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
        auto *before = static_cast<unsigned char *>( wolf::push( arena, 16, 16 ) );
        REQUIRE( before != nullptr );
        CHECK( wolf::push( arena, SIZE_MAX, 1 ) == nullptr );
        CHECK( wolf::push( arena, 16, size_t{ 1 } << 63 ) == nullptr );
        auto *after = static_cast<unsigned char *>( wolf::push( arena, 16, 16 ) );
        CHECK( after == before + 16 ); // the same block: nothing was mapped
        wolf::reset( arena );
    }

    TEST_CASE( "a block that cannot be mapped returns null and the arena keeps working" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
        CHECK( wolf::push( arena, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( wolf::push( arena, 16, 16 ) != nullptr );
        wolf::reset( arena );
    }

    TEST_CASE( "reset keeps the first block, releases the others and re-zeroes what was used" )
    {
        wolf::Arena arena{ .blockSize = 64 * KiB };
        auto *head = static_cast<unsigned char *>( wolf::push( arena, 1024, 16 ) );
        auto *tail = static_cast<unsigned char *>( wolf::push( arena, 16 * KiB, 4096 ) ); // second page of the first block
        std::memset( head, 0xCD, 1024 );
        std::memset( tail, 0xCD, 16 * KiB );
        auto *second = static_cast<unsigned char *>( wolf::push( arena, 60 * KiB, 16 ) );
        auto *big = static_cast<unsigned char *>( wolf::push( arena, 256 * KiB, 16 ) );
        wolf::reset( arena );

        CHECK( state( second ) == MEM_FREE );
        CHECK( state( big ) == MEM_FREE );
        CHECK( head[ 0 ] == 0 );
#ifdef NDEBUG
        CHECK( state( tail ) == MEM_COMMIT );
        CHECK( tail[ 0 ] == 0 );
#else
        CHECK( state( tail ) == MEM_RESERVE ); // a stale pointer faults at its use
#endif
        CHECK( wolf::push( arena, 1024, 16 ) == head );
        auto *again = static_cast<unsigned char *>( wolf::push( arena, 16 * KiB, 4096 ) );
        CHECK( again == tail );
        CHECK( again[ ( 16 * KiB ) - 1 ] == 0 );
        wolf::reset( arena );
    }
}
