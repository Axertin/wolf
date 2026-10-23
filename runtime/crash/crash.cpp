#include "crash/crash.hpp"

#include <windows.h>

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <utility>

#include "core/threading.hpp"
#include "hooks/resolve.hpp"
#include "log/log.hpp"

namespace {

constexpr DWORD kTerminateCode = 0xE0574F4C;
constexpr DWORD kReportWaitMs = 10000;
constexpr int kMaxIdBytes = 128; // the longest mod id the crash line names

using MiniDumpWriteDumpFn = BOOL( WINAPI * )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                              PMINIDUMP_CALLBACK_INFORMATION );

MiniDumpWriteDumpFn writeDump = nullptr;
DWORD dbghelpError = 0;
wchar_t logsDir[ MAX_PATH ] = {};
HANDLE wake = nullptr; // set by the filter for the reporter
HANDLE done = nullptr; // manual reset: set once the report is written, for every waiting filter

// The thread inside the filter, 0 until the first crash. Only that thread hands a fault to the reporter. A plain LONG with interlocked intrinsics, because an
// overflowed thread runs the handoff on the little stack it has left, and debug builds call through std::atomic's functions.
volatile LONG faulting = 0;
DWORD reporterThread = 0;
EXCEPTION_POINTERS *faultPointers = nullptr;
WolfMod *faultMod = nullptr;

LONG waitForReport()
{
    return WaitForSingleObject( done, kReportWaitMs ) == WAIT_OBJECT_0 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// Hands the fault to the reporter and waits for the report. The thread that faults first reports; one faulting later waits for that report; a fault inside
// this function or on the reporter thread passes straight on, since waiting would wait on itself.
LONG handOff( EXCEPTION_POINTERS *pointers )
{
    const DWORD self = GetCurrentThreadId();
    if ( self == reporterThread )
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if ( const auto first = static_cast<DWORD>( InterlockedCompareExchange( &faulting, static_cast<LONG>( self ), 0 ) ); first != 0 )
    {
        return first == self ? EXCEPTION_CONTINUE_SEARCH : waitForReport();
    }
    faultPointers = pointers;
    faultMod = wolf::g_dispatchMod;
    SetEvent( wake );
    return waitForReport();
}

LONG WINAPI crashFilter( EXCEPTION_POINTERS *pointers )
{
    return handOff( pointers );
}

// A stack overflow can end its thread before the unhandled-exception filter runs, as it does under Wine, so it is reported at first chance. The process then
// ends with the overflow's code once the report is written.
LONG WINAPI overflowHandler( EXCEPTION_POINTERS *pointers )
{
    if ( pointers->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW )
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if ( handOff( pointers ) == EXCEPTION_EXECUTE_HANDLER )
    {
        TerminateProcess( GetCurrentProcess(), EXCEPTION_STACK_OVERFLOW );
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// The line goes out before the dump, so the code and address are on record even when the dump then blocks on a lock the faulting thread holds.
void report()
{
    const EXCEPTION_RECORD &record = *faultPointers->ExceptionRecord;
    const auto thread = static_cast<DWORD>( faulting );

    SYSTEMTIME time;
    GetLocalTime( &time );
    char name[ 64 ];
    snprintf( name, sizeof( name ), "crash-%04u%02u%02u-%02u%02u%02u.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond );
    // Room for the dump text, the mod id cut to kMaxIdBytes and the overlap caveat.
    char outcome[ 320 ];
    const int printed = writeDump != nullptr ? snprintf( outcome, sizeof( outcome ), "dump logs\\%s", name )
                                             : snprintf( outcome, sizeof( outcome ), "dump failed with error %lu", dbghelpError );
    const int length = std::clamp( printed, 0, static_cast<int>( sizeof( outcome ) ) - 1 );
    if ( faultMod != nullptr )
    {
        snprintf( outcome + length, sizeof( outcome ) - static_cast<size_t>( length ), ", dispatching %.*s%s", kMaxIdBytes, faultMod->id,
                  wolf::dispatchOverlapSeen() ? " (a dispatch overlap was logged, so the dispatching mod may be wrong)" : "" );
    }

    char path[ MAX_PATH ];
    uintptr_t base = 0;
    const auto address = reinterpret_cast<uintptr_t>( record.ExceptionAddress );
    if ( const char *module = wolf::moduleName( record.ExceptionAddress, path, &base ); module != nullptr )
    {
        wolf::writeLog( nullptr, WOLF_LOG_ERROR, "crash: exception 0x%08lX at %s+0x%llX on thread %lu, %s", record.ExceptionCode, module,
                        static_cast<unsigned long long>( address - base ), thread, outcome );
    }
    else
    {
        wolf::writeLog( nullptr, WOLF_LOG_ERROR, "crash: exception 0x%08lX at 0x%llX on thread %lu, %s", record.ExceptionCode,
                        static_cast<unsigned long long>( address ), thread, outcome );
    }
    if ( writeDump == nullptr )
    {
        return;
    }

    wchar_t dumpPath[ MAX_PATH ];
    swprintf( dumpPath, MAX_PATH, L"%ls%hs", logsDir, name );
    const HANDLE file = CreateFileW( dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    DWORD error = GetLastError();
    if ( file != INVALID_HANDLE_VALUE )
    {
        MINIDUMP_EXCEPTION_INFORMATION info{ .ThreadId = thread, .ExceptionPointers = faultPointers, .ClientPointers = FALSE };
        const auto type = static_cast<MINIDUMP_TYPE>( MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory );
        error = writeDump( GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr ) != 0 ? 0 : GetLastError();
        CloseHandle( file );
    }
    if ( error != 0 )
    {
        wolf::writeLog( nullptr, WOLF_LOG_ERROR, "crash: dump logs\\%s failed with error %lu", name, error );
    }
}

// The terminate handler. It reports through UnhandledExceptionFilter, which runs WOLF's filter, instead of raising: MSVC's terminate calls its handler inside
// an exception guard and then aborts, so an exception raised here would never reach the filter.
[[noreturn]] void reportTerminate()
{
    CONTEXT context{};
    RtlCaptureContext( &context );
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kTerminateCode;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = reinterpret_cast<void *>( &reportTerminate ); // the dump holds the stack that called terminate
    EXCEPTION_POINTERS pointers{ .ExceptionRecord = &record, .ContextRecord = &context };
    UnhandledExceptionFilter( &pointers );
    TerminateProcess( GetCurrentProcess(), kTerminateCode );
    std::unreachable();
}

// Takes no WOLF lock: the faulting thread may hold the runtime lock.
DWORD WINAPI reporter( void * /*unused*/ )
{
    WaitForSingleObject( wake, INFINITE );
    report();
    SetEvent( done );
    return 0;
}

} // namespace

namespace wolf {

void crashStart( const wchar_t *gameDir )
{
    swprintf( logsDir, MAX_PATH, L"%lslogs\\", gameDir );
    CreateDirectoryW( logsDir, nullptr );

    wchar_t path[ MAX_PATH ];
    const UINT length = GetSystemDirectoryW( path, MAX_PATH );
    swprintf( path + length, MAX_PATH - length, L"\\dbghelp.dll" );
    if ( const HMODULE dbghelp = LoadLibraryW( path ); dbghelp != nullptr )
    {
        writeDump = reinterpret_cast<MiniDumpWriteDumpFn>( GetProcAddress( dbghelp, "MiniDumpWriteDump" ) );
    }
    if ( writeDump == nullptr )
    {
        dbghelpError = GetLastError();
        writeLog( nullptr, WOLF_LOG_ERROR, "dbghelp.dll did not load; crashes write no dump" );
    }

    wake = CreateEventW( nullptr, FALSE, FALSE, nullptr );
    done = CreateEventW( nullptr, TRUE, FALSE, nullptr );
    if ( const HANDLE thread = CreateThread( nullptr, 0, &reporter, nullptr, 0, &reporterThread ); thread != nullptr )
    {
        CloseHandle( thread );
    }
    AddVectoredExceptionHandler( 1, &overflowHandler );
    SetUnhandledExceptionFilter( &crashFilter );
    std::set_terminate( &reportTerminate );
}

void crashReassert()
{
    if ( SetUnhandledExceptionFilter( &crashFilter ) != &crashFilter )
    {
        writeLog( nullptr, WOLF_LOG_WARN, "the unhandled exception filter was replaced during flower_startup; WOLF's is back" );
    }
}

} // namespace wolf
