// wolf-crashchild.exe <dir> <mode>: starts the file log and the crash reporter in <dir>, then does what mode names. crash_test.cpp runs it and reads what the
// reporter left behind.
//   fault-worker  a second thread writes through a null pointer
//   terminate     a C++ exception escapes a noexcept function
//   fault-many    eight threads released together each write through a null pointer
//   overflow      a second thread recurses until its stack runs out
//   replaced      another filter replaces WOLF's, then crashReassert runs, and the process exits 0
#include <windows.h>

#include <atomic>
#include <crash/crash.hpp>
#include <cstring>
#include <log/file_log.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

volatile int *nowhere = nullptr;

TEST_NOINLINE void raiseError()
{
    throw std::runtime_error( "escapes" );
}

// The throw sits behind a call, so the compiler cannot tell at compile time that this noexcept function throws. The escape is what the terminate mode tests.
// NOLINTNEXTLINE(bugprone-exception-escape)
void escape() noexcept
{
    raiseError();
}

volatile bool deeper = true;

// Recurses until the stack runs out, which is the point; the volatile frame and flag keep the compiler from turning it into a loop.
// NOLINTNEXTLINE(misc-no-recursion)
TEST_NOINLINE int recurse( int depth )
{
    volatile char frame[ 512 ] = {};
    frame[ depth % 512 ] = static_cast<char>( depth );
    return deeper ? recurse( depth + 1 ) + frame[ 0 ] : 0;
}

LONG WINAPI otherFilter( EXCEPTION_POINTERS * /*pointers*/ )
{
    return EXCEPTION_CONTINUE_SEARCH;
}

int run( int argc, char **argv )
{
    if ( argc != 3 )
    {
        return 2;
    }
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    const std::string narrow = argv[ 1 ];
    const std::wstring dir( narrow.begin(), narrow.end() );
    if ( !wolf::startFileLog( dir.c_str() ) )
    {
        return 3;
    }
    wolf::crashStart( dir.c_str() );

    const std::string mode = argv[ 2 ];
    if ( mode == "fault-worker" )
    {
        std::thread worker( [] { *nowhere = 1; } );
        worker.join();
    }
    else if ( mode == "fault-many" )
    {
        constexpr int kThreads = 8;
        std::atomic<int> ready{ 0 };
        std::vector<std::thread> threads;
        threads.reserve( kThreads );
        for ( int i = 0; i < kThreads; ++i )
        {
            threads.emplace_back( [ & ] {
                ready.fetch_add( 1 );
                while ( ready.load() < kThreads )
                {
                }
                *nowhere = 1;
            } );
        }
        for ( std::thread &thread : threads )
        {
            thread.join();
        }
    }
    else if ( mode == "overflow" )
    {
        std::thread worker( [] { recurse( 0 ); } );
        worker.join();
    }
    else if ( mode == "terminate" )
    {
        escape();
    }
    else if ( mode == "replaced" )
    {
        SetUnhandledExceptionFilter( &otherFilter );
        wolf::crashReassert();
        return 0;
    }
    return 4;
}

} // namespace

int main( int argc, char **argv )
{
    try
    {
        return run( argc, argv );
    }
    catch ( ... )
    {
        return 5;
    }
}
