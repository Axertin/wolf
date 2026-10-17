#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <core/threading.hpp>
#include <cstdint>
#include <frame/run_queue.hpp>
#include <numeric>
#include <vector>

#include "support.hpp"

namespace {

WolfMod mod{};

struct Call
{
    WolfMod *mod = nullptr;
    std::vector<uint8_t> bytes;
};

std::vector<Call> calls;

void recordCall( WolfMod *caller, void *data, uint32_t len )
{
    const auto *bytes = static_cast<const uint8_t *>( data );
    calls.push_back( { caller, std::vector<uint8_t>( bytes, bytes + len ) } );
}

void callAgain( WolfMod *caller, void *data, uint32_t len )
{
    recordCall( caller, data, len );
    wolf::runInGame( caller, recordCall, nullptr, 0 );
}

WolfMod *seen = nullptr;

std::vector<uintptr_t> addresses;

void recordAddress( WolfMod * /*caller*/, void *data, uint32_t /*len*/ )
{
    addresses.push_back( reinterpret_cast<uintptr_t>( data ) );
}

void checkMod( WolfMod * /*caller*/, void * /*data*/, uint32_t /*len*/ )
{
    seen = wolf::g_dispatchMod;
}

} // namespace

TEST_SUITE( "run queue" )
{
    TEST_CASE( "a call runs once, at the next run step, with its own copy of the payload" )
    {
        wolf::runQueued();
        calls.clear();
        std::array<uint8_t, 64> payload{};
        std::ranges::iota( payload, uint8_t{ 1 } );
        const auto original = payload;
        CHECK( wolf::runInGame( &mod, recordCall, payload.data(), 64 ) == 1 );
        payload.fill( 0 );
        CHECK( calls.empty() );
        wolf::runQueued();
        REQUIRE( calls.size() == 1 );
        CHECK( calls[ 0 ].mod == &mod );
        CHECK( calls[ 0 ].bytes == std::vector<uint8_t>( original.begin(), original.end() ) );
        wolf::runQueued();
        CHECK( calls.size() == 1 );
    }

    TEST_CASE( "calls run in call order" )
    {
        wolf::runQueued();
        calls.clear();
        for ( uint8_t i = 1; i <= 3; ++i )
        {
            CHECK( wolf::runInGame( &mod, recordCall, &i, 1 ) == 1 );
        }
        wolf::runQueued();
        std::vector<uint8_t> firstBytes;
        firstBytes.reserve( calls.size() );
        for ( const Call &call : calls )
        {
            firstBytes.push_back( call.bytes.at( 0 ) );
        }
        CHECK( firstBytes == std::vector<uint8_t>{ 1, 2, 3 } );
    }

    TEST_CASE( "a call with no payload runs with length 0" )
    {
        wolf::runQueued();
        calls.clear();
        CHECK( wolf::runInGame( &mod, recordCall, nullptr, 0 ) == 1 );
        wolf::runQueued();
        REQUIRE( calls.size() == 1 );
        CHECK( calls[ 0 ].bytes.empty() );
    }

    TEST_CASE( "a call made while the queue runs waits for the next run step" )
    {
        wolf::runQueued();
        calls.clear();
        CHECK( wolf::runInGame( &mod, callAgain, nullptr, 0 ) == 1 );
        wolf::runQueued();
        CHECK( calls.size() == 1 );
        wolf::runQueued();
        CHECK( calls.size() == 2 );
    }

    TEST_CASE( "calls from several threads while the queue runs each run once, in each thread's order" )
    {
        constexpr int kThreads = 8;
        constexpr int kEach = 40000;
        wolf::runQueued();
        calls.clear();
        std::atomic<int> accepted{ 0 };
        raceThreads(
            kThreads,
            [ &accepted ]( int t ) {
                for ( int i = 0; i < kEach; ++i )
                {
                    const std::array<uint8_t, 3> payload = { static_cast<uint8_t>( t ), static_cast<uint8_t>( i & 0xFF ), static_cast<uint8_t>( i >> 8 ) };
                    accepted += wolf::runInGame( &mod, recordCall, payload.data(), 3 );
                }
            },
            wolf::runQueued );
        wolf::runQueued();
        wolf::runQueued();
        CHECK( accepted.load() == kThreads * kEach );
        REQUIRE( calls.size() == static_cast<size_t>( kThreads ) * kEach );
        std::array<int, kThreads> next{};
        bool inOrder = true;
        for ( const Call &call : calls )
        {
            const int t = call.bytes.at( 0 );
            const int i = call.bytes.at( 1 ) | ( call.bytes.at( 2 ) << 8 );
            inOrder = inOrder && i == next.at( t )++;
        }
        CHECK( inOrder );
        CHECK( std::ranges::all_of( next, []( int n ) { return n == kEach; } ) );
    }

    TEST_CASE( "a call runs with its mod as the dispatching mod" )
    {
        wolf::runQueued();
        seen = nullptr;
        CHECK( wolf::runInGame( &mod, checkMod, nullptr, 0 ) == 1 );
        wolf::runQueued();
        CHECK( seen == &mod );
        CHECK( wolf::g_dispatchMod == nullptr );
    }

    TEST_CASE( "the payload pointer a call receives is 16-byte aligned" )
    {
        wolf::runQueued();
        addresses.clear();
        const std::array<uint8_t, 8> payload{};
        for ( int i = 0; i < 5; ++i )
        {
            CHECK( wolf::runInGame( &mod, recordAddress, payload.data(), 8 ) == 1 );
        }
        wolf::runQueued();
        REQUIRE( addresses.size() == 5 );
        CHECK( std::ranges::all_of( addresses, []( uintptr_t a ) { return a % 16 == 0; } ) );
    }
}
