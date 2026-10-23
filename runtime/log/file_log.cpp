#include "log/file_log.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "log/log.hpp"
#include "mods/mod.hpp"

namespace {

std::atomic<HANDLE> file{ INVALID_HANDLE_VALUE };
// End of the bytes handed out so far. Wine does not serialise appends from several threads, so each line reserves its range here and writes at that offset,
// and lines from several threads never overlap on Windows or under Proton.
std::atomic<uint64_t> fileEnd{ 0 };

const char *levelName( int32_t level )
{
    switch ( level )
    {
    case WOLF_LOG_DEBUG:
        return "DEBUG";
    case WOLF_LOG_INFO:
        return "INFO";
    case WOLF_LOG_WARN:
        return "WARN";
    default:
        return "ERROR";
    }
}

void writeLine( WolfMod *mod, int32_t level, const char *line )
{
    SYSTEMTIME time;
    GetLocalTime( &time );
    // The line itself is at most 1023 characters; the rest is room for the prefix.
    constexpr int kSize = 1100;
    char buffer[ kSize ];
    // The thread id and level are padded, so the levels share a column for thread ids up to six digits.
    const int length = snprintf( buffer, kSize, "%02u:%02u:%02u.%03u %6lu %-5s [%s] %s\r\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                                 GetCurrentThreadId(), levelName( level ), mod != nullptr ? mod->id : "wolf", line );
    if ( length <= 0 )
    {
        return;
    }
    // A cut line keeps the buffer's last byte for snprintf's terminator.
    const auto size = static_cast<DWORD>( length < kSize ? length : kSize - 1 );
    const uint64_t offset = fileEnd.fetch_add( size );
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>( offset );
    at.OffsetHigh = static_cast<DWORD>( offset >> 32 );
    DWORD written = 0;
    WriteFile( file.load( std::memory_order_acquire ), buffer, size, &written, &at );
}

} // namespace

namespace wolf {

bool startFileLog( const wchar_t *gameDir )
{
    wchar_t path[ MAX_PATH ];
    if ( swprintf( path, MAX_PATH, L"%lslogs", gameDir ) < 0 )
    {
        return false;
    }
    if ( CreateDirectoryW( path, nullptr ) == 0 && GetLastError() != ERROR_ALREADY_EXISTS )
    {
        return false;
    }
    if ( swprintf( path, MAX_PATH, L"%lslogs\\wolf.log", gameDir ) < 0 )
    {
        return false;
    }
    const HANDLE opened =
        CreateFileW( path, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( opened == INVALID_HANDLE_VALUE )
    {
        return false;
    }
    fileEnd.store( 0 );
    file.store( opened, std::memory_order_release );
    setLogSink( &writeLine );
    return true;
}

} // namespace wolf
