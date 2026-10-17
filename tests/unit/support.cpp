#include "support.hpp"

#include <arena/tier.hpp>
#include <atomic>
#include <frame/boundary.hpp>
#include <log/log.hpp>
#include <mutex>
#include <thread>

namespace {

std::mutex mutex;
std::vector<LogLine> recorded;

void record( WolfMod *mod, int32_t level, const char *line )
{
    const std::scoped_lock guard( mutex );
    recorded.push_back( LogLine{ .mod = mod, .level = level, .text = line } );
}

} // namespace

namespace logs {

void start()
{
    wolf::setLogSink( &record );
    const std::scoped_lock guard( mutex );
    recorded.clear();
}

std::vector<LogLine> lines()
{
    const std::scoped_lock guard( mutex );
    return recorded;
}

size_t count( std::string_view needle )
{
    const std::scoped_lock guard( mutex );
    size_t n = 0;
    for ( const LogLine &line : recorded )
    {
        if ( line.text.find( needle ) != std::string::npos )
        {
            ++n;
        }
    }
    return n;
}

} // namespace logs

void raceThreads( int count, const std::function<void( int )> &fn, const std::function<void()> &mainStep )
{
    std::atomic<int> arrived{ 0 };
    std::atomic<int> running{ count };
    const auto barrier = [ & ] {
        arrived.fetch_add( 1 );
        while ( arrived.load() < count + 1 )
        {
            std::this_thread::yield();
        }
    };
    std::vector<std::thread> threads;
    threads.reserve( static_cast<size_t>( count ) );
    for ( int i = 0; i < count; ++i )
    {
        threads.emplace_back( [ &, i ] {
            barrier();
            fn( i );
            running.fetch_sub( 1 );
        } );
    }
    barrier();
    while ( mainStep && running.load() != 0 )
    {
        mainStep();
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }
}

void drainRuntime()
{
    wolf::tierEnd( WOLF_TIER_SESSION );
    wolf::frameBoundary();
    wolf::frameBoundary();
}
