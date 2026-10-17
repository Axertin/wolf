#include <doctest/doctest.h>

#include <algorithm>
#include <core/threading.hpp>
#include <cstdint>
#include <numeric>
#include <span>
#include <tables/table.hpp>
#include <vector>

#include "support.hpp"

namespace {

void *append( wolf::Table &table, WolfMod *mod, uint32_t *index )
{
    const wolf::RuntimeLock lock;
    return wolf::tableAppend( table, mod, index );
}

WolfMod mod{};

} // namespace

TEST_SUITE( "table" )
{
    TEST_CASE( "the first entry is index 1 and entries come back zeroed" )
    {
        static wolf::Table table{ .name = "first", .stride = 16, .onceBit = 30 };
        uint32_t index = 0;
        auto *entry = static_cast<unsigned char *>( append( table, &mod, &index ) );
        REQUIRE( entry != nullptr );
        CHECK( index == 1 );
        CHECK( wolf::tableEnd( table ) == 2 );
        CHECK( wolf::tableAt( table, 1 ) == entry );
        CHECK( std::ranges::all_of( std::span( entry, 16 ), []( unsigned char c ) { return c == 0; } ) );
    }

    TEST_CASE( "crossing a chunk boundary adds a chunk and earlier entries do not move" )
    {
        static wolf::Table table{ .name = "chunks", .stride = 8, .onceBit = 30 };
        uint32_t index = 0;

        while ( index < wolf::kChunkEntries - 1 )
        {
            REQUIRE( append( table, &mod, &index ) != nullptr );
        }
        const void *const p4095 = wolf::tableAt( table, 4095 );
        REQUIRE( append( table, &mod, &index ) != nullptr );
        CHECK( index == wolf::kChunkEntries );
        REQUIRE( append( table, &mod, &index ) != nullptr );
        CHECK( index == wolf::kChunkEntries + 1 );
        CHECK( wolf::tableAt( table, 4095 ) == p4095 );
    }

    TEST_CASE( "a full table returns null and logs once per mod and table" )
    {
        static wolf::Table table{ .name = "full", .stride = 4, .onceBit = 30 };
        logs::start();
        WolfMod other{};
        uint32_t index = 0;
        bool filled = true;
        for ( uint32_t i = 1; i < wolf::kMaxEntries; ++i )
        {
            filled = append( table, &mod, &index ) != nullptr && filled;
        }
        REQUIRE( filled );
        CHECK( wolf::tableEnd( table ) == wolf::kMaxEntries );
        CHECK( append( table, &mod, &index ) == nullptr );
        CHECK( append( table, &mod, &index ) == nullptr );
        CHECK( logs::count( "full table is full" ) == 1 );
        CHECK( append( table, &other, &index ) == nullptr );
        CHECK( logs::count( "full table is full" ) == 2 );

        static wolf::Table second{ .name = "second", .stride = 4, .onceBit = 29 };
        filled = true;
        for ( uint32_t i = 1; i < wolf::kMaxEntries; ++i )
        {
            filled = append( second, &mod, &index ) != nullptr && filled;
        }
        REQUIRE( filled );
        CHECK( append( second, &mod, &index ) == nullptr );
        CHECK( append( second, &mod, &index ) == nullptr );
        CHECK( logs::count( "second table is full" ) == 1 );
    }

    TEST_CASE( "passing the expected size warns at it and at each doubling" )
    {
        static wolf::Table table{ .name = "warn", .stride = 8, .expected = 8, .onceBit = 30 };
        logs::start();
        uint32_t index = 0;
        while ( index < 8 )
        {
            REQUIRE( append( table, &mod, &index ) != nullptr );
        }
        CHECK( logs::count( "warn table passed" ) == 0 );
        REQUIRE( append( table, &mod, &index ) != nullptr );
        REQUIRE( logs::count( "warn table passed 8 entries" ) == 1 );
        CHECK( logs::lines().back().mod == &mod );
        CHECK( logs::lines().back().level == WOLF_LOG_WARN );
        while ( index < 16 )
        {
            REQUIRE( append( table, &mod, &index ) != nullptr );
        }
        CHECK( logs::count( "warn table passed" ) == 1 );
        REQUIRE( append( table, &mod, &index ) != nullptr );
        CHECK( logs::count( "warn table passed 16 entries" ) == 1 );
        CHECK( logs::count( "warn table passed" ) == 2 );
    }

    TEST_CASE( "a table with no expected size never warns" )
    {
        static wolf::Table table{ .name = "quiet", .stride = 8, .onceBit = 30 };
        logs::start();
        uint32_t index = 0;
        for ( int i = 0; i < 100; ++i )
        {
            REQUIRE( append( table, &mod, &index ) != nullptr );
        }
        CHECK( logs::count( "quiet table passed" ) == 0 );
    }

    TEST_CASE( "clear forgets every entry and the next append is index 1, zeroed" )
    {
        static wolf::Table table{ .name = "clear", .stride = 8, .onceBit = 30 };
        uint32_t index = 0;
        for ( int i = 0; i < 3; ++i )
        {
            auto *e = static_cast<unsigned char *>( append( table, &mod, &index ) );
            REQUIRE( e != nullptr );
            std::ranges::fill( std::span( e, 8 ), 0xFF );
        }
        wolf::tableClear( table );
        CHECK( wolf::tableEnd( table ) <= 1 );
        auto *entry = static_cast<unsigned char *>( append( table, &mod, &index ) );
        REQUIRE( entry != nullptr );
        CHECK( index == 1 );
        CHECK( std::ranges::all_of( std::span( entry, 8 ), []( unsigned char c ) { return c == 0; } ) );
    }
}
