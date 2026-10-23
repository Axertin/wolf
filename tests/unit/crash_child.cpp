// wolf-crashchild.exe <dir> <mode>: starts the file log and the crash reporter in <dir>, then does what mode names. crash_test.cpp runs it and reads what the
// reporter left behind.
//   fault-worker  a second thread writes through a null pointer
//   terminate     a C++ exception escapes a noexcept function
//   terminate-mod wolf-terminate-mod.dll's init lets a C++ exception escape a noexcept function in the mod's own C runtime
//   fault-many    eight threads released together each write through a null pointer
//   overflow      a second thread recurses until its stack runs out
//   fault-in-mod  the main thread writes through a null pointer inside a DispatchScope for a mod with id "crashy"
//   fault-long-id-after-overlap  a second thread enters a DispatchScope with no mod while the main thread is inside one for a mod with a 300 byte id, then
//                 the main thread writes through a null pointer
//   fault-in-watch  a watch for a mod with id "watcher" is armed at a frame boundary, its page is released, and the next boundary compares it
//   replaced      another filter replaces WOLF's, then crashReassert runs; the process exits 0 when WOLF's filter is back, else 10
//   throw-in-detour  a second thread calls a binding hook detour whose lambda throws
//   terminate-in-callback  a second thread calls a binding site trampoline whose lambda lets an exception escape a noexcept function
//   terminate-in-detour  a second thread calls a binding hook detour whose lambda lets an exception escape a noexcept function
#include <windows.h>

#include <wolf/wolf.hpp>

#include <atomic>
#include <core/threading.hpp>
#include <crash/crash.hpp>
#include <frame/boundary.hpp>
#include <log/file_log.hpp>
#include <memory/memory.hpp>
#include <mods/api.hpp>
#include <mods/mod.hpp>
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

[[noreturn]] TEST_NOINLINE void raiseError()
{
    throw std::runtime_error( "escapes" );
}

// The throw sits behind a call, so the compiler cannot tell at compile time that this noexcept function throws. The escape is what the terminate mode tests.
// NOLINTNEXTLINE(bugprone-exception-escape)
[[noreturn]] void escape() noexcept
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

void ignoreWatch( WolfMod * /*mod*/, void * /*ctx*/, void * /*user*/ )
{}

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
    const std::string mode = argv[ 2 ];
    if ( !wolf::startFileLog( dir.c_str() ) )
    {
        return 3;
    }
    wolf::crashStart( dir.c_str() );
    const LPTOP_LEVEL_EXCEPTION_FILTER wolfFilter = SetUnhandledExceptionFilter( nullptr );
    SetUnhandledExceptionFilter( wolfFilter );

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
    else if ( mode == "terminate-mod" )
    {
        const HMODULE mod = LoadLibraryW( L"wolf-terminate-mod.dll" );
        if ( mod == nullptr )
        {
            return 6;
        }
        const auto init = reinterpret_cast<WolfModInitFn>( GetProcAddress( mod, "wolf_mod_init" ) );
        if ( init == nullptr )
        {
            return 7;
        }
        static WolfMod record{ .id = "terminate" };
        init( wolf::wolfApi(), &record );
    }
    else if ( mode == "throw-in-detour" )
    {
        const auto throwing = []( int ( * /*original*/ )( int ), int /*value*/ ) -> int { raiseError(); };
        using Slot = wolf::detail::HookSlot<int( int ), decltype( throwing )>;
        std::thread worker( [] { Slot::detour( 1 ); } );
        worker.join();
    }
    else if ( mode == "terminate-in-callback" )
    {
        const auto callback = []( WolfTickCtx & /*tick*/ ) { escape(); };
        std::thread worker( [] {
            WolfTickCtx tick{};
            wolf::detail::siteCall<wolf::site::Tick, decltype( callback )>( nullptr, &tick, nullptr );
        } );
        worker.join();
    }
    else if ( mode == "terminate-in-detour" )
    {
        const auto terminating = []( int ( * /*original*/ )( int ), int /*value*/ ) -> int { escape(); };
        using Slot = wolf::detail::HookSlot<int( int ), decltype( terminating )>;
        std::thread worker( [] { Slot::detour( 1 ); } );
        worker.join();
    }
    else if ( mode == "fault-in-mod" || mode == "fault-long-id-after-overlap" )
    {
        static const std::string longId( 300, 'x' );
        static WolfMod crashy{ .id = "crashy" };
        static WolfMod longMod{ .id = longId.c_str() };
        const wolf::DispatchScope scope( mode == "fault-in-mod" ? &crashy : &longMod );
        if ( mode != "fault-in-mod" )
        {
            std::thread other( [] { const wolf::DispatchScope inner( nullptr ); } );
            other.join();
        }
        *nowhere = 1;
    }
    else if ( mode == "fault-in-watch" )
    {
        static WolfMod watcher{ .id = "watcher" };
        wolf::memoryInit();
        void *page = VirtualAlloc( nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        if ( page == nullptr || wolf::wolfApi()->watch( &watcher, page, 4, &ignoreWatch, nullptr, WOLF_LIFETIME_PROCESS ) == 0 )
        {
            return 8;
        }
        wolf::frameBoundary();
        VirtualFree( page, 0, MEM_RELEASE );
        wolf::frameBoundary();
    }
    else if ( mode == "replaced" )
    {
        SetUnhandledExceptionFilter( &otherFilter );
        wolf::crashReassert();
        return SetUnhandledExceptionFilter( nullptr ) == wolfFilter ? 0 : 10;
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
