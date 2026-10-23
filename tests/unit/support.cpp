#include "support.hpp"

#include <windows.h>

#include <doctest/doctest.h>

#include <arena/lifetime.hpp>
#include <atomic>
#include <frame/boundary.hpp>
#include <fstream>
#include <iterator>
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
    wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
    wolf::frameBoundary();
    wolf::frameBoundary();
}

std::wstring freshTempDir()
{
    static std::atomic<int> counter{ 0 };
    wchar_t base[ MAX_PATH ];
    const DWORD length = GetTempPathW( MAX_PATH, base );
    // A process id can repeat across runs, so a name an earlier run left behind is skipped.
    for ( ;; )
    {
        const std::wstring dir =
            std::wstring( base, length ) + L"wolf-tests-" + std::to_wstring( GetCurrentProcessId() ) + L"-" + std::to_wstring( counter.fetch_add( 1 ) ) + L"\\";
        if ( CreateDirectoryW( dir.c_str(), nullptr ) != 0 )
        {
            return dir;
        }
        if ( GetLastError() != ERROR_ALREADY_EXISTS )
        {
            FAIL( "cannot create a directory under the temporary directory" );
        }
    }
}

std::string readFile( const std::wstring &path )
{
    std::ifstream in( path.c_str(), std::ios::binary );
    return { std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
}
