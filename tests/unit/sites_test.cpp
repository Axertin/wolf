#include <doctest/doctest.h>
#include <wolf/sites.h>

#include <arena/lifetime.hpp>
#include <atomic>
#include <core/threading.hpp>
#include <cstdint>
#include <frame/boundary.hpp>
#include <frame/run_queue.hpp>
#include <mods/mod.hpp>
#include <sites/sites.hpp>
#include <string>
#include <tables/registry.hpp>
#include <vector>

#include "support.hpp"

namespace {

// What each callback saw, in the order the callbacks ran.
std::vector<std::string> seen;

struct Named
{
    const char *name = nullptr;
};

void recordName( WolfMod * /*mod*/, void * /*ctx*/, void *user )
{
    seen.emplace_back( static_cast<Named *>( user )->name );
}

void countCall( WolfMod * /*mod*/, void * /*ctx*/, void *user )
{
    ++*static_cast<int *>( user );
}

void begin()
{
    wolf::sitesInit();
    seen.clear();
}

void tick()
{
    wolf::siteTickEnter();
    wolf::siteTickReturn();
}

} // namespace

TEST_SUITE( "sites" )
{
    TEST_CASE( "a callback takes effect at the next boundary and stops at the boundary after its removal" )
    {
        begin();
        WolfMod mod{};
        int calls = 0;
        WolfTickCtx ctx{ .size = sizeof( WolfTickCtx ), .reserved = 0, .frame = 0 };
        const WolfHandle handle = wolf::siteOn( &mod, WOLF_SITE_TICK, &countCall, &calls, WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        wolf::siteDispatch( wolf::Site::Tick, &ctx );
        CHECK( calls == 0 );
        wolf::frameBoundary();
        wolf::siteDispatch( wolf::Site::Tick, &ctx );
        CHECK( calls == 1 );
        wolf::registryRemove( &mod, handle );
        wolf::siteDispatch( wolf::Site::Tick, &ctx );
        CHECK( calls == 2 );
        wolf::frameBoundary();
        wolf::siteDispatch( wolf::Site::Tick, &ctx );
        CHECK( calls == 2 );
        drainRuntime();
    }

    TEST_CASE( "callbacks run in load order, then in registration order" )
    {
        begin();
        WolfMod early{ .loadIndex = 0 };
        WolfMod late{ .loadIndex = 1 };
        Named names[] = { { "1a" }, { "0a" }, { "1b" }, { "0b" } };
        std::vector<WolfHandle> handles{
            wolf::siteOn( &late, WOLF_SITE_SESSION_END, &recordName, &names[ 0 ], WOLF_LIFETIME_PROCESS ),
            wolf::siteOn( &early, WOLF_SITE_SESSION_END, &recordName, &names[ 1 ], WOLF_LIFETIME_PROCESS ),
            wolf::siteOn( &late, WOLF_SITE_SESSION_END, &recordName, &names[ 2 ], WOLF_LIFETIME_PROCESS ),
            wolf::siteOn( &early, WOLF_SITE_SESSION_END, &recordName, &names[ 3 ], WOLF_LIFETIME_PROCESS ),
        };
        wolf::frameBoundary();
        WolfSessionEndCtx ctx{ .size = sizeof( WolfSessionEndCtx ) };
        wolf::siteDispatch( wolf::Site::SessionEnd, &ctx );
        CHECK( seen == std::vector<std::string>{ "0a", "0b", "1a", "1b" } );
        wolf::registryRemove( &late, handles[ 0 ] );
        wolf::registryRemove( &early, handles[ 1 ] );
        wolf::registryRemove( &late, handles[ 2 ] );
        wolf::registryRemove( &early, handles[ 3 ] );
        drainRuntime();
    }

    TEST_CASE( "each callback runs as its own mod's dispatch" )
    {
        begin();
        WolfMod mod{};
        const WolfMod *during = nullptr;
        const WolfHandle handle = wolf::siteOn(
            &mod, WOLF_SITE_TICK_POST, []( WolfMod * /*mod*/, void * /*ctx*/, void *user ) { *static_cast<const WolfMod **>( user ) = wolf::g_dispatchMod; },
            static_cast<void *>( &during ), WOLF_LIFETIME_PROCESS );
        wolf::frameBoundary();
        wolf::siteTickReturn();
        CHECK( during == &mod );
        CHECK( wolf::g_dispatchMod == nullptr );
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    TEST_CASE( "tick and tick.post carry the same frame, which grows by one per tick" )
    {
        begin();
        WolfMod mod{};
        std::vector<uint64_t> frames;
        const auto recordFrame = []( WolfMod * /*mod*/, void *ctx, void *user ) {
            static_cast<std::vector<uint64_t> *>( user )->push_back( static_cast<WolfTickCtx *>( ctx )->frame );
        };
        const WolfHandle a = wolf::siteOn( &mod, WOLF_SITE_TICK, recordFrame, &frames, WOLF_LIFETIME_PROCESS );
        const WolfHandle b = wolf::siteOn( &mod, WOLF_SITE_TICK_POST, recordFrame, &frames, WOLF_LIFETIME_PROCESS );
        tick();
        tick();
        tick();
        REQUIRE( frames.size() == 6 );
        const uint64_t f = frames[ 0 ];
        CHECK( f > 0 );
        CHECK( frames == std::vector<uint64_t>{ f, f, f + 1, f + 1, f + 2, f + 2 } );
        wolf::registryRemove( &mod, a );
        wolf::registryRemove( &mod, b );
        drainRuntime();
    }

    TEST_CASE( "room.change reports the previous room as from, carried across a session end" )
    {
        begin();
        WolfMod mod{};
        std::vector<WolfRoomChangeCtx> changes;
        const WolfHandle handle = wolf::siteOn(
            &mod, WOLF_SITE_ROOM_CHANGE,
            []( WolfMod * /*mod*/, void *ctx, void *user ) {
                static_cast<std::vector<WolfRoomChangeCtx> *>( user )->push_back( *static_cast<WolfRoomChangeCtx *>( ctx ) );
            },
            &changes, WOLF_LIFETIME_PROCESS );
        wolf::frameBoundary();
        // from is process state, so the first change here only sets a known room.
        wolf::siteRoomChangeEnter();
        wolf::siteRoomChangeReturn( 0x122 );
        wolf::siteRoomChangeEnter();
        wolf::siteRoomChangeReturn( 0xf02 );
        wolf::siteSessionEnd();
        wolf::frameBoundary();
        wolf::siteRoomChangeEnter();
        wolf::siteRoomChangeReturn( 0x100 );
        REQUIRE( changes.size() == 3 );
        CHECK( changes[ 0 ].to == 0x122 );
        CHECK( changes[ 1 ].size == sizeof( WolfRoomChangeCtx ) );
        CHECK( changes[ 1 ].from == 0x122 );
        CHECK( changes[ 1 ].to == 0xf02 );
        CHECK( changes[ 2 ].from == 0xf02 );
        CHECK( changes[ 2 ].to == 0x100 );
        wolf::registryRemove( &mod, handle );
        drainRuntime();
    }

    TEST_CASE( "a room-lifetime callback never sees the room change that ends its room" )
    {
        begin();
        WolfMod mod{};
        int roomCalls = 0;
        int sessionCalls = 0;
        wolf::siteOn( &mod, WOLF_SITE_ROOM_CHANGE, &countCall, &roomCalls, WOLF_LIFETIME_ROOM );
        wolf::siteOn( &mod, WOLF_SITE_ROOM_CHANGE, &countCall, &sessionCalls, WOLF_LIFETIME_SESSION );
        wolf::frameBoundary();
        wolf::siteRoomChangeEnter();
        wolf::siteRoomChangeReturn( 0x300 );
        CHECK( roomCalls == 0 );
        CHECK( sessionCalls == 1 );
        drainRuntime();
    }

    TEST_CASE( "session.end runs for the ending session and what it registers or allocates belongs to the next one" )
    {
        begin();
        static WolfMod mod{};
        static int endCalls = 0;
        static int queuedRuns = 0;
        static int warmStarts = 0;
        static int sessionLifetimeStarts = 0;
        static int roomLifetimeChanges = 0;
        static bool queuedBeforeStart = false;
        static WolfHandle startHandle = 0;
        static uint8_t *prepared = nullptr;
        endCalls = 0;
        wolf::siteOn(
            &mod, WOLF_SITE_SESSION_END,
            []( WolfMod *self, void * /*ctx*/, void * /*user*/ ) {
                ++endCalls;
                startHandle = wolf::siteOn(
                    self, WOLF_SITE_SESSION_START,
                    []( WolfMod * /*mod*/, void *ctx, void * /*user*/ ) {
                        warmStarts += static_cast<WolfSessionStartCtx *>( ctx )->warm;
                        queuedBeforeStart = queuedRuns == 1;
                    },
                    nullptr, WOLF_LIFETIME_PROCESS );
                wolf::siteOn( self, WOLF_SITE_SESSION_START, &countCall, &sessionLifetimeStarts, WOLF_LIFETIME_SESSION );
                wolf::siteOn( self, WOLF_SITE_ROOM_CHANGE, &countCall, &roomLifetimeChanges, WOLF_LIFETIME_ROOM );
                prepared = static_cast<uint8_t *>( wolf::lifetimeAlloc( self, WOLF_LIFETIME_SESSION, 64, 16 ) );
                prepared[ 0 ] = 0x5A;
                wolf::runInGame( self, []( WolfMod * /*mod*/, void * /*data*/, uint32_t /*len*/ ) { ++queuedRuns; }, nullptr, 0 );
            },
            nullptr, WOLF_LIFETIME_SESSION );
        wolf::frameBoundary();
        wolf::siteSessionEnd();
        CHECK( endCalls == 1 );
        wolf::siteSessionStart( true );
        CHECK( queuedRuns == 1 );
        CHECK( warmStarts == 1 );
        CHECK( queuedBeforeStart );
        CHECK( sessionLifetimeStarts == 1 );
        // The next flower_tick entry frees the ended session's memory; what session.end allocated belongs to the new session and survives it.
        wolf::frameBoundary();
        CHECK( prepared[ 0 ] == 0x5A );
        wolf::siteRoomChangeReturn( 0x400 );
        CHECK( roomLifetimeChanges == 1 );
        // session.end itself was the ended session's, so it does not run for the new one.
        wolf::siteSessionEnd();
        CHECK( endCalls == 1 );
        wolf::registryRemove( &mod, startHandle );
        drainRuntime();
    }

    TEST_CASE( "item.add callbacks rewrite delta and mark the call handled, the last mod having the final word" )
    {
        begin();
        WolfMod early{ .loadIndex = 0 };
        WolfMod late{ .loadIndex = 1 };
        const WolfHandle a = wolf::siteOn(
            &late, WOLF_SITE_ITEM_ADD,
            []( WolfMod * /*mod*/, void *ctx, void * /*user*/ ) {
                auto *item = static_cast<WolfItemAddCtx *>( ctx );
                item->delta = 7;
                item->handled = 1;
            },
            nullptr, WOLF_LIFETIME_PROCESS );
        const WolfHandle b = wolf::siteOn(
            &early, WOLF_SITE_ITEM_ADD, []( WolfMod * /*mod*/, void *ctx, void * /*user*/ ) { static_cast<WolfItemAddCtx *>( ctx )->delta = 5; }, nullptr,
            WOLF_LIFETIME_PROCESS );
        wolf::frameBoundary();
        WolfItemAddCtx ctx{ .size = sizeof( WolfItemAddCtx ), .itemId = 0x22, .delta = 1, .handled = 0, .fromMod = nullptr };
        wolf::siteItemAdd( ctx );
        CHECK( ctx.delta == 7 );
        CHECK( ctx.handled == 1 );
        CHECK( ctx.itemId == 0x22 );
        wolf::registryRemove( &late, a );
        wolf::registryRemove( &early, b );
        drainRuntime();
    }

    TEST_CASE( "subscriptions added and removed from several threads against boundaries and dispatch each run once per dispatch while live" )
    {
        begin();
        constexpr int kThreads = 4;
        constexpr int kRounds = 30000;
        static WolfMod mods[ kThreads ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 }, { .loadIndex = 3 } };
        // Per callback: the frame it last ran in, how often it ran twice in one frame, and how often it ran at all.
        struct Seen
        {
            uint64_t lastFrame = 0;
            int twice = 0;
            int runs = 0;
        };
        static Seen churned[ kThreads ][ 2 ];
        static Seen kept[ kThreads ];
        const auto check = []( WolfMod * /*mod*/, void *ctx, void *user ) {
            auto *last = static_cast<Seen *>( user );
            const uint64_t frame = static_cast<WolfTickCtx *>( ctx )->frame;
            if ( last->lastFrame == frame )
            {
                ++last->twice;
            }
            last->lastFrame = frame;
            ++last->runs;
        };
        std::atomic<int> refused{ 0 };
        WolfHandle keptHandles[ kThreads ] = {};
        raceThreads(
            kThreads,
            [ & ]( int t ) {
                for ( int round = 0; round < kRounds; ++round )
                {
                    const WolfHandle handle = wolf::siteOn( &mods[ t ], WOLF_SITE_TICK, check, &churned[ t ][ round & 1 ], WOLF_LIFETIME_PROCESS );
                    if ( handle == 0 )
                    {
                        refused.fetch_add( 1 );
                        continue;
                    }
                    wolf::registryRemove( &mods[ t ], handle );
                }
                // The last one stays, so a live subscription the race dropped from its site's list would show.
                keptHandles[ t ] = wolf::siteOn( &mods[ t ], WOLF_SITE_TICK, check, &kept[ t ], WOLF_LIFETIME_PROCESS );
            },
            [] { tick(); } );
        CHECK( refused.load() == 0 );
        tick();
        int twice = 0;
        for ( int t = 0; t < kThreads; ++t )
        {
            twice += churned[ t ][ 0 ].twice + churned[ t ][ 1 ].twice + kept[ t ].twice;
            kept[ t ].runs = 0;
        }
        CHECK( twice == 0 );
        const int churnedBefore = churned[ 0 ][ 0 ].runs + churned[ 0 ][ 1 ].runs;
        tick();
        for ( int t = 0; t < kThreads; ++t )
        {
            CHECK( kept[ t ].runs == 1 );
            wolf::registryRemove( &mods[ t ], keptHandles[ t ] );
        }
        // Every churned subscription was removed, so none of them runs any more.
        CHECK( churned[ 0 ][ 0 ].runs + churned[ 0 ][ 1 ].runs == churnedBefore );
        drainRuntime();
    }
}
