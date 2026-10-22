#include <windows.h>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support.hpp"

namespace {

struct ChildRun
{
    DWORD exitCode = 0;
    std::vector<std::wstring> dumps; // file names of logs\crash-*.dmp with a nonzero size
    std::string log;                 // logs\wolf.log
};

// Runs wolf-crashchild.exe, which sits next to this exe, in a fresh directory and collects what it left there.
ChildRun runChild( const char *mode )
{
    wchar_t self[ MAX_PATH ];
    const DWORD length = GetModuleFileNameW( nullptr, self, MAX_PATH );
    std::wstring exe( self, length );
    exe = exe.substr( 0, exe.find_last_of( L'\\' ) + 1 ) + L"wolf-crashchild.exe";
    const std::wstring dir = freshTempDir();
    const std::string modeText = mode;
    // dir ends in a backslash, which would escape the closing quote; a doubled one reads as one.
    std::wstring commandLine = L"\"" + exe + L"\" \"" + dir + L"\\\" " + std::wstring( modeText.begin(), modeText.end() );

    ChildRun run;
    STARTUPINFOW startup{};
    startup.cb = sizeof( STARTUPINFOW );
    PROCESS_INFORMATION process{};
    REQUIRE( CreateProcessW( exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process ) );
    REQUIRE( WaitForSingleObject( process.hProcess, 30000 ) == WAIT_OBJECT_0 );
    GetExitCodeProcess( process.hProcess, &run.exitCode );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );

    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW( ( dir + L"logs\\crash-*.dmp" ).c_str(), &found );
    if ( search != INVALID_HANDLE_VALUE )
    {
        do
        {
            if ( found.nFileSizeLow != 0 || found.nFileSizeHigh != 0 )
            {
                run.dumps.emplace_back( found.cFileName );
            }
        } while ( FindNextFileW( search, &found ) != 0 );
        FindClose( search );
    }
    run.log = readFile( dir + L"logs\\wolf.log" );
    return run;
}

size_t occurrences( const std::string &text, const std::string &needle )
{
    size_t n = 0;
    for ( size_t at = text.find( needle ); at != std::string::npos; at = text.find( needle, at + 1 ) )
    {
        ++n;
    }
    return n;
}

std::string narrow( const std::wstring &text )
{
    std::string out;
    for ( const wchar_t c : text )
    {
        out.push_back( static_cast<char>( c ) );
    }
    return out;
}

} // namespace

TEST_SUITE( "crash" )
{
    TEST_CASE( "a fault on a worker thread writes a dump and one line naming the module and offset" )
    {
        const ChildRun run = runChild( "fault-worker" );
        CHECK( run.exitCode == 0xC0000005 );
        REQUIRE( run.dumps.size() == 1 );
        CHECK( occurrences( run.log, "crash: exception" ) == 1 );
        CHECK( occurrences( run.log, "crash: exception 0xC0000005 at wolf-crashchild.exe+0x" ) == 1 );
        CHECK( occurrences( run.log, ", dump logs\\" + narrow( run.dumps[ 0 ] ) ) == 1 );
    }

    TEST_CASE( "faults on several threads at once write one dump and one line" )
    {
        const ChildRun run = runChild( "fault-many" );
        CHECK( run.exitCode == 0xC0000005 );
        CHECK( run.dumps.size() == 1 );
        CHECK( occurrences( run.log, "crash: exception" ) == 1 );
        CHECK( occurrences( run.log, "crash: exception 0xC0000005 at wolf-crashchild.exe+0x" ) == 1 );
    }

    TEST_CASE( "a stack overflow on a worker thread still gets a dump and a line" )
    {
        // Wine dispatches a stack overflow only when enough stack is left past the guard page, which depends on the frame sizes of the build, and otherwise
        // ends the thread with no handler run. Native Windows always dispatches it, so the case runs there.
        if ( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "wine_get_version" ) != nullptr )
        {
            MESSAGE( "skipped under Wine, which may end an overflowed thread without dispatching the exception" );
            return;
        }
        const ChildRun run = runChild( "overflow" );
        CHECK( run.exitCode == 0xC00000FD );
        CHECK( run.dumps.size() == 1 );
        CHECK( occurrences( run.log, "crash: exception 0xC00000FD at " ) == 1 );
    }

    TEST_CASE( "a C++ exception that escapes reaches the filter as 0xE0574F4C" )
    {
        const ChildRun run = runChild( "terminate" );
        CHECK( run.exitCode == 0xE0574F4C );
        CHECK( run.dumps.size() == 1 );
        CHECK( occurrences( run.log, "crash: exception" ) == 1 );
        CHECK( occurrences( run.log, "crash: exception 0xE0574F4C at " ) == 1 );
    }

    TEST_CASE( "reasserting the filter after something replaced it logs once" )
    {
        const ChildRun run = runChild( "replaced" );
        CHECK( run.exitCode == 0 );
        CHECK( run.dumps.empty() );
        CHECK( occurrences( run.log, "the unhandled exception filter was replaced during flower_startup; WOLF's is back" ) == 1 );
    }
}
