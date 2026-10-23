#include <windows.h>

#include <doctest/doctest.h>
#include <wolf/sites.h>
#include <wolf/wolf.h>

#include <algorithm>
#include <arena/lifetime.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <frame/boundary.hpp>
#include <memory/memory.hpp>
#include <mods/api.hpp>
#include <mods/mod.hpp>
#include <string>
#include <string_view>
#include <tables/registry.hpp>
#include <vector>

#include "support.hpp"

namespace {

constexpr size_t kPage = 4096;

// Constant data in the test exe's .rdata, which is image memory.
const std::array<uint8_t, 16> kImageBytes = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

// Read through a volatile pointer, so the compiler cannot fold reads of memory the runtime changed.
std::vector<uint8_t> bytesAt( const void *addr, size_t n )
{
    const auto *at = static_cast<const volatile uint8_t *>( addr );
    std::vector<uint8_t> bytes( n );
    for ( size_t i = 0; i < n; ++i )
    {
        bytes[ i ] = at[ i ];
    }
    return bytes;
}

std::vector<uint8_t> filled( size_t n, uint8_t value )
{
    return std::vector<uint8_t>( n, value ); // NOLINT(modernize-return-braced-init-list): braces would build a two-element vector
}

DWORD protectionOf( const void *addr )
{
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery( addr, &info, sizeof( info ) );
    return info.Protect;
}

// A zeroed committed page with the given protection. Never freed.
uint8_t *freshPage( DWORD protect = PAGE_READWRITE )
{
    auto *page = static_cast<uint8_t *>( VirtualAlloc( nullptr, kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
    REQUIRE( page != nullptr );
    DWORD old = 0;
    REQUIRE( VirtualProtect( page, kPage, protect, &old ) );
    return page;
}

void begin()
{
    wolf::memoryInit();
}

struct Fired
{
    uint32_t n = 0;
    uint32_t previous = 0;
};
std::vector<Fired> fired;
std::atomic<int> badContexts{ 0 };
std::atomic<int> fires{ 0 };

void recordWatch( WolfMod * /*mod*/, void *ctx, void * /*user*/ )
{
    const auto &watch = *static_cast<const WolfWatchCtx *>( ctx );
    uint32_t previous = 0;
    std::memcpy( &previous, watch.previous, sizeof( previous ) );
    fired.push_back( { watch.n, previous } );
}

void checkWatchCtx( WolfMod * /*mod*/, void *ctx, void * /*user*/ )
{
    const auto &watch = *static_cast<const WolfWatchCtx *>( ctx );
    ++fires;
    uint32_t previous = 0;
    uint32_t current = 0;
    if ( watch.size == sizeof( WolfWatchCtx ) && watch.n == 4 && watch.addr != nullptr && watch.previous != nullptr )
    {
        std::memcpy( &previous, watch.previous, sizeof( previous ) );
        std::memcpy( &current, watch.addr, sizeof( current ) );
    }
    // The words count up from 1, so a snapshot that kept its earlier value is nonzero and below the current one; a snapshot in a reset arena reads 0.
    if ( previous == 0 || previous >= current )
    {
        ++badContexts;
    }
}

void setU32( uint8_t *at, uint32_t value )
{
    std::memcpy( at, &value, sizeof( value ) );
}

// A watch callback that removes its own watch and adds one on the next word.
WolfMod chainMod{ .id = "chain" };
WolfHandle chainFirst = 0;
WolfHandle chainSecond = 0;
uint8_t *chainPage = nullptr;

} // namespace

TEST_SUITE( "memory" )
{
    TEST_CASE( "write copies into read-only and executable memory and keeps the protection" )
    {
        begin();
        const std::array<uint8_t, 4> value = { 0xC3, 1, 2, 3 };
        for ( const DWORD protect : { DWORD{ PAGE_READONLY }, DWORD{ PAGE_EXECUTE_READ } } )
        {
            CAPTURE( protect );
            uint8_t *page = freshPage( protect );
            CHECK( wolf::memoryWrite( page + 100, value.data(), value.size() ) == 1 );
            CHECK( bytesAt( page + 100, value.size() ) == std::vector<uint8_t>( value.begin(), value.end() ) );
            CHECK( protectionOf( page ) == protect );
        }
    }

    TEST_CASE( "write refuses memory that is not committed" )
    {
        begin();
        const std::array<uint8_t, 8> value = { 1, 2, 3, 4, 5, 6, 7, 8 };
        void *reserved = VirtualAlloc( nullptr, kPage, MEM_RESERVE, PAGE_READWRITE );
        REQUIRE( reserved != nullptr );
        CHECK( wolf::memoryWrite( reserved, value.data(), value.size() ) == 0 );
        uint8_t *page = freshPage();
        CHECK( wolf::memoryWrite( page + kPage - 4, value.data(), value.size() ) == 0 );
        CHECK( bytesAt( page + kPage - 4, 4 ) == filled( 4, 0 ) );
        for ( const DWORD protect : { DWORD{ PAGE_NOACCESS }, DWORD{ PAGE_READWRITE | PAGE_GUARD } } )
        {
            CAPTURE( protect );
            CHECK( wolf::memoryWrite( freshPage( protect ), value.data(), value.size() ) == 0 );
        }
    }

    TEST_CASE( "write crosses regions of different protection and keeps each one's" )
    {
        begin();
        auto *pages = static_cast<uint8_t *>( VirtualAlloc( nullptr, 2 * kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
        REQUIRE( pages != nullptr );
        DWORD old = 0;
        REQUIRE( VirtualProtect( pages, kPage, PAGE_READONLY, &old ) );
        REQUIRE( VirtualProtect( pages + kPage, kPage, PAGE_EXECUTE_READ, &old ) );
        const std::array<uint8_t, 8> value = { 1, 2, 3, 4, 5, 6, 7, 8 };
        CHECK( wolf::memoryWrite( pages + kPage - 4, value.data(), value.size() ) == 1 );
        CHECK( bytesAt( pages + kPage - 4, value.size() ) == std::vector<uint8_t>( value.begin(), value.end() ) );
        CHECK( protectionOf( pages ) == PAGE_READONLY );
        CHECK( protectionOf( pages + kPage ) == PAGE_EXECUTE_READ );
    }

    TEST_CASE( "writes from several threads to one read-only page never fault" )
    {
        begin();
        constexpr int kThreads = 4;
        constexpr uint32_t kEach = 1500;
        uint8_t *page = freshPage( PAGE_READONLY );
        WolfMod mod{ .id = "patcher" };
        const std::array<uint8_t, 8> patched = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE };
        std::atomic<int> refused{ 0 };
        raceThreads(
            kThreads,
            [ & ]( int t ) {
                for ( uint32_t k = 1; k <= kEach; ++k )
                {
                    const uint64_t value = ( static_cast<uint64_t>( t ) << 32 ) | k;
                    if ( wolf::memoryWrite( page + ( static_cast<size_t>( t ) * 8 ), &value, sizeof( value ) ) == 0 )
                    {
                        ++refused;
                    }
                }
            },
            // The main thread's patch ops flip and restore the same page under the boundary's hold of the lock.
            [ & ] {
                const WolfHandle handle = wolf::patchAdd( &mod, page + 64, patched.data(), patched.size(), WOLF_LIFETIME_PROCESS );
                wolf::frameBoundary();
                wolf::registryRemove( &mod, handle );
                wolf::frameBoundary();
            } );
        CHECK( refused.load() == 0 );
        CHECK( bytesAt( page + 64, patched.size() ) == filled( patched.size(), 0 ) );
        for ( int t = 0; t < kThreads; ++t )
        {
            CAPTURE( t );
            uint64_t value = 0;
            std::memcpy( &value, page + ( static_cast<size_t>( t ) * 8 ), sizeof( value ) );
            CHECK( value == ( ( static_cast<uint64_t>( t ) << 32 ) | kEach ) );
        }
        CHECK( protectionOf( page ) == PAGE_READONLY );
    }

    TEST_CASE( "a patch applies at the boundary and remove restores it" )
    {
        begin();
        WolfMod mod{ .id = "patcher" };
        uint8_t *page = freshPage( PAGE_READONLY );
        const std::array<uint8_t, 4> value = { 9, 8, 7, 6 };
        const WolfHandle handle = wolf::patchAdd( &mod, page + 16, value.data(), value.size(), WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        CHECK( bytesAt( page + 16, 4 ) == filled( 4, 0 ) );
        wolf::frameBoundary();
        CHECK( bytesAt( page + 16, 4 ) == std::vector<uint8_t>( value.begin(), value.end() ) );
        wolf::registryRemove( &mod, handle );
        CHECK( bytesAt( page + 16, 4 ) == std::vector<uint8_t>( value.begin(), value.end() ) );
        wolf::frameBoundary();
        CHECK( bytesAt( page + 16, 4 ) == filled( 4, 0 ) );
        CHECK( protectionOf( page ) == PAGE_READONLY );
        drainRuntime();
    }

    TEST_CASE( "a heap patch is forgotten at its lifetime's end and an image patch restored" )
    {
        begin();
        WolfMod mod{ .id = "patcher" };
        uint8_t *page = freshPage();
        auto *image = const_cast<uint8_t *>( kImageBytes.data() ) + 4;
        const std::array<uint8_t, 4> value = { 0xAA, 0xBB, 0xCC, 0xDD };
        const std::vector<uint8_t> patched( value.begin(), value.end() );
        REQUIRE( wolf::patchAdd( &mod, page, value.data(), value.size(), WOLF_LIFETIME_SESSION ) != 0 );
        REQUIRE( wolf::patchAdd( &mod, image, value.data(), value.size(), WOLF_LIFETIME_SESSION ) != 0 );
        wolf::frameBoundary();
        CHECK( bytesAt( page, 4 ) == patched );
        CHECK( bytesAt( image, 4 ) == patched );
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        wolf::frameBoundary();
        CHECK( bytesAt( page, 4 ) == patched );
        CHECK( bytesAt( image, 4 ) == std::vector<uint8_t>{ 5, 6, 7, 8 } );
        drainRuntime();
    }

    TEST_CASE( "a patch overlapping a live patch is not applied" )
    {
        begin();
        WolfMod first{ .id = "first" };
        WolfMod second{ .id = "second" };
        uint8_t *page = freshPage();
        const std::array<uint8_t, 4> ones = { 1, 1, 1, 1 };
        const std::array<uint8_t, 4> twos = { 2, 2, 2, 2 };
        const WolfHandle a = wolf::patchAdd( &first, page, ones.data(), ones.size(), WOLF_LIFETIME_PROCESS );
        const WolfHandle b = wolf::patchAdd( &second, page + 2, twos.data(), twos.size(), WOLF_LIFETIME_PROCESS );
        REQUIRE( a != 0 );
        REQUIRE( b != 0 );
        wolf::frameBoundary();
        CHECK( bytesAt( page, 6 ) == std::vector<uint8_t>{ 1, 1, 1, 1, 0, 0 } );
        wolf::registryRemove( &first, a );
        wolf::frameBoundary();
        CHECK( bytesAt( page, 6 ) == filled( 6, 0 ) );
        wolf::registryRemove( &second, b );
        drainRuntime();
    }

    TEST_CASE( "a watch whose memory is decommitted before its boundary is not armed" )
    {
        begin();
        fired.clear();
        WolfMod mod{ .id = "watcher" };
        uint8_t *page = freshPage();
        const WolfHandle handle = wolf::watchAdd( &mod, page, 4, recordWatch, nullptr, WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        REQUIRE( VirtualFree( page, kPage, MEM_DECOMMIT ) );
        wolf::frameBoundary();
        CHECK( fired.empty() );
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    TEST_CASE( "a patch whose memory is decommitted before its boundary does not fault the boundary" )
    {
        begin();
        WolfMod mod{ .id = "patcher" };
        uint8_t *page = freshPage();
        const std::array<uint8_t, 4> value = { 1, 2, 3, 4 };
        const WolfHandle handle = wolf::patchAdd( &mod, page, value.data(), value.size(), WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        REQUIRE( VirtualFree( page, kPage, MEM_DECOMMIT ) );
        wolf::frameBoundary();
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    // A writable page followed by a read-only view of a read-only mapping, which VirtualProtect cannot make writable, so a write across them fails after
    // its first region.
    TEST_CASE( "a patch whose write fails partway leaves the memory as it was" )
    {
        begin();
        constexpr size_t kGranule = size_t{ 64 } * 1024;
        HANDLE mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, nullptr, PAGE_READONLY, 0, static_cast<DWORD>( kPage ), nullptr );
        REQUIRE( mapping != nullptr );
        auto *base = static_cast<uint8_t *>( VirtualAlloc( nullptr, 2 * kGranule, MEM_RESERVE, PAGE_NOACCESS ) );
        REQUIRE( base != nullptr );
        REQUIRE( VirtualFree( base, 0, MEM_RELEASE ) );
        REQUIRE( VirtualAlloc( base, kGranule, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) == base );
        REQUIRE( MapViewOfFileEx( mapping, FILE_MAP_READ, 0, 0, kPage, base + kGranule ) == base + kGranule );
        uint8_t *at = base + kGranule - 4;
        const std::array<uint8_t, 8> value = { 1, 2, 3, 4, 5, 6, 7, 8 };
        WolfMod mod{ .id = "patcher" };
        const WolfHandle handle = wolf::patchAdd( &mod, at, value.data(), value.size(), WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        CHECK( bytesAt( at, value.size() ) == filled( value.size(), 0 ) );
        WolfMod writer{};
        CHECK( wolf::wolfApi()->write( &writer, at, value.data(), value.size() ) == 0 );
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    TEST_CASE( "patch copies its bytes at the call" )
    {
        begin();
        WolfMod mod{ .id = "patcher" };
        uint8_t *page = freshPage();
        std::array<uint8_t, 4> source = { 5, 5, 5, 5 };
        const WolfHandle handle = wolf::patchAdd( &mod, page, source.data(), source.size(), WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        source.fill( 7 );
        wolf::frameBoundary();
        CHECK( bytesAt( page, 4 ) == filled( 4, 5 ) );
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    // The session ends on the main step, between boundaries, as the game ends it; the patches race it from threads outside game context.
    TEST_CASE( "patches registered from several threads while sessions end are applied whole or not at all" )
    {
        begin();
        constexpr int kPatchers = 3;
        constexpr int kEach = 2000;
        constexpr size_t kSlot = 16;
        std::array<WolfMod, kPatchers> mods{};
        mods[ 0 ].id = "patcher 1";
        mods[ 1 ].id = "patcher 2";
        mods[ 2 ].id = "patcher 3";
        uint8_t *page = freshPage();
        raceThreads(
            kPatchers,
            [ & ]( int t ) {
                std::array<uint8_t, kSlot> value{};
                value.fill( static_cast<uint8_t>( t + 1 ) );
                for ( int k = 0; k < kEach; ++k )
                {
                    wolf::patchAdd( &mods.at( static_cast<size_t>( t ) ), page + ( static_cast<size_t>( t ) * kSlot ), value.data(), value.size(),
                                    WOLF_LIFETIME_SESSION );
                }
            },
            [] {
                wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
                wolf::frameBoundary();
            } );
        wolf::frameBoundary();
        wolf::frameBoundary();
        for ( int t = 0; t < kPatchers; ++t )
        {
            CAPTURE( t );
            const std::vector<uint8_t> slot = bytesAt( page + ( static_cast<size_t>( t ) * kSlot ), kSlot );
            CHECK( ( slot == filled( kSlot, static_cast<uint8_t>( t + 1 ) ) || slot == filled( kSlot, 0 ) ) );
        }
        drainRuntime();
    }

    TEST_CASE( "the table refuses bad write and patch calls" )
    {
        begin();
        const WolfApi &api = *wolf::wolfApi();
        static std::array<WolfMod, 6> mods{};
        uint32_t value = 0;
        void *reserved = VirtualAlloc( nullptr, kPage, MEM_RESERVE, PAGE_READWRITE );
        REQUIRE( reserved != nullptr );
        for ( int round = 0; round < 2; ++round )
        {
            CHECK( api.write( &mods.at( 0 ), nullptr, &value, 4 ) == 0 );
            CHECK( api.write( &mods.at( 1 ), reserved, &value, 4 ) == 0 );
            CHECK( api.patch( &mods.at( 2 ), &value, &value, 4, WOLF_LIFETIME_FRAME ) == 0 );
            CHECK( api.patch( &mods.at( 3 ), &value, nullptr, 4, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.patch( &mods.at( 4 ), reserved, &value, 4, WOLF_LIFETIME_PROCESS ) == 0 );
        }
        const uint32_t written = 0x1234U;
        CHECK( api.write( &mods.at( 5 ), &value, &written, sizeof( written ) ) == 1 );
        CHECK( value == written );
        drainRuntime();
    }

    TEST_CASE( "execute-only memory is not readable for a watch or a patch" )
    {
        begin();
        const WolfApi &api = *wolf::wolfApi();
        static std::array<WolfMod, 2> mods{};
        uint8_t *page = freshPage( PAGE_EXECUTE );
        REQUIRE( protectionOf( page ) == PAGE_EXECUTE );
        const uint32_t value = 0;
        CHECK( api.watch( &mods.at( 0 ), page, 4, recordWatch, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
        CHECK( api.patch( &mods.at( 1 ), page, &value, 4, WOLF_LIFETIME_PROCESS ) == 0 );
        drainRuntime();
    }

    TEST_CASE( "a watch fires once per change with the previous bytes" )
    {
        begin();
        fired.clear();
        WolfMod watcher{ .id = "watcher" };
        uint8_t *page = freshPage();
        const WolfHandle handle = wolf::watchAdd( &watcher, page, 4, recordWatch, nullptr, WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        for ( const uint32_t value : { 1U, 1U, 2U } )
        {
            setU32( page, value );
            wolf::frameBoundary();
        }
        REQUIRE( fired.size() == 2 );
        CHECK( ( fired[ 0 ].n == 4 && fired[ 0 ].previous == 0 ) );
        CHECK( ( fired[ 1 ].n == 4 && fired[ 1 ].previous == 1 ) );
        wolf::registryRemove( &watcher, handle );
        drainRuntime();
    }

    TEST_CASE( "a watch callback can remove its own watch and add another" )
    {
        begin();
        fired.clear();
        chainPage = freshPage();
        chainFirst = wolf::watchAdd(
            &chainMod, chainPage, 4,
            []( WolfMod *mod, void *ctx, void *user ) {
                recordWatch( mod, ctx, user );
                wolf::registryRemove( &chainMod, chainFirst );
                chainSecond = wolf::watchAdd( &chainMod, chainPage + 8, 4, recordWatch, nullptr, WOLF_LIFETIME_PROCESS );
            },
            nullptr, WOLF_LIFETIME_PROCESS );
        REQUIRE( chainFirst != 0 );
        wolf::frameBoundary();
        setU32( chainPage, 1 );
        wolf::frameBoundary();
        CHECK( fired.size() == 1 );
        setU32( chainPage, 2 ); // the first watch is gone, the second takes its snapshot at this boundary
        wolf::frameBoundary();
        CHECK( fired.size() == 1 );
        setU32( chainPage + 8, 7 );
        wolf::frameBoundary();
        REQUIRE( fired.size() == 2 );
        CHECK( fired[ 1 ].previous == 0 );
        wolf::registryRemove( &chainMod, chainSecond );
        drainRuntime();
    }

    TEST_CASE( "a room watch stops when the room ends" )
    {
        begin();
        fired.clear();
        WolfMod watcher{ .id = "room watcher" };
        uint8_t *page = freshPage();
        REQUIRE( wolf::watchAdd( &watcher, page, 4, recordWatch, nullptr, WOLF_LIFETIME_ROOM ) != 0 );
        wolf::frameBoundary();
        setU32( page, 1 );
        wolf::frameBoundary();
        CHECK( fired.size() == 1 );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        setU32( page, 2 );
        wolf::frameBoundary();
        CHECK( fired.size() == 1 );
        drainRuntime();
    }

    TEST_CASE( "watch refuses unreadable memory and sizes outside 1 to 64 KiB" )
    {
        begin();
        const WolfApi &api = *wolf::wolfApi();
        static std::array<WolfMod, 5> mods{};
        const void *reserved = VirtualAlloc( nullptr, kPage, MEM_RESERVE, PAGE_READWRITE );
        REQUIRE( reserved != nullptr );
        const uint8_t *page = freshPage();
        auto *big = static_cast<uint8_t *>( VirtualAlloc( nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
        REQUIRE( big != nullptr );
        for ( int round = 0; round < 2; ++round )
        {
            CHECK( api.watch( &mods.at( 0 ), reserved, 4, recordWatch, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.watch( &mods.at( 1 ), page, 0, recordWatch, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.watch( &mods.at( 2 ), big, 65537, recordWatch, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.watch( &mods.at( 3 ), page, 4, recordWatch, nullptr, WOLF_LIFETIME_FRAME ) == 0 );
        }
        const WolfHandle one = api.watch( &mods.at( 4 ), page, 1, recordWatch, nullptr, WOLF_LIFETIME_PROCESS );
        const WolfHandle most = api.watch( &mods.at( 4 ), big, 65536, recordWatch, nullptr, WOLF_LIFETIME_PROCESS );
        CHECK( one != 0 );
        CHECK( most != 0 );
        api.remove( &mods.at( 4 ), one );
        api.remove( &mods.at( 4 ), most );
        drainRuntime();
    }

    // Rooms end on the main step, between boundaries, as the game ends them; the watches race it from threads outside game context.
    TEST_CASE( "watches registered while rooms end never fault and fire with the earlier bytes" )
    {
        begin();
        badContexts = 0;
        fires = 0;
        constexpr int kWatchers = 3;
        constexpr uint32_t kSteps = 600;
        std::atomic<bool> stop{ false };
        std::array<WolfMod, kWatchers> mods{};
        mods[ 0 ].id = "watcher 1";
        mods[ 1 ].id = "watcher 2";
        mods[ 2 ].id = "watcher 3";
        uint8_t *page = freshPage();
        uint32_t tick = 0;
        raceThreads(
            kWatchers,
            [ & ]( int t ) {
                while ( !stop.load() )
                {
                    wolf::watchAdd( &mods.at( static_cast<size_t>( t ) ), page + ( static_cast<size_t>( t ) * 8 ), 4, checkWatchCtx, nullptr,
                                    WOLF_LIFETIME_ROOM );
                }
            },
            [ & ] {
                setU32( page, ++tick );
                setU32( page + 8, tick );
                setU32( page + 16, tick );
                if ( tick % 3 == 0 ) // watches armed at one boundary compare changed bytes at the next unless their room ends between
                {
                    wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
                }
                wolf::frameBoundary();
                stop.store( tick >= kSteps );
            } );
        wolf::frameBoundary();
        drainRuntime();
        CHECK( fires.load() > 0 );
        CHECK( badContexts.load() == 0 );
    }
}
