#include "boot/boot.hpp"

#include <wolf_version.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>

#include "boot/detours.hpp"
#include "hooks/hooks.hpp"
#include "log/file_log.hpp"
#include "log/log.hpp"
#include "sites/sites.hpp"

namespace {

wchar_t gameDir[ MAX_PATH ] = {};

constexpr char kMismatchFormat[] = "main.dll is TimeDateStamp 0x%08X, SizeOfImage 0x%X; WOLF supports 0x60E54536, 0x1150000. No mods will load.";
char mismatch[ sizeof( kMismatchFormat ) + 16 ] = {};

// Shown from a thread of its own, so DllMain never waits on a window.
DWORD WINAPI showMismatch( void * /*unused*/ )
{
    MessageBoxA( nullptr, mismatch, "WOLF", MB_OK | MB_ICONWARNING );
    return 0;
}

// The parts of ntdll's loader notification WOLF reads (LDR_DLL_LOADED_NOTIFICATION_DATA and its UNICODE_STRING), which the SDK headers do not declare.
struct LoaderString
{
    USHORT length = 0; // bytes, not characters
    USHORT maximumLength = 0;
    PWSTR buffer = nullptr;
};

struct LoadedDll
{
    ULONG flags = 0;
    const LoaderString *fullName = nullptr;
    const LoaderString *baseName = nullptr;
    void *base = nullptr;
    ULONG sizeOfImage = 0;
};

constexpr ULONG kDllLoaded = 1;
using DllNotification = VOID( CALLBACK * )( ULONG reason, const LoadedDll *dll, void *context );
using RegisterDllNotification = LONG( NTAPI * )( ULONG flags, DllNotification fn, void *context, const void **cookie );

// Set by whichever of DllMain and the loader notification sees main.dll first; the other then does nothing.
std::atomic<bool> mainSeen{ false };

bool isMainDll( const LoaderString *name )
{
    constexpr wchar_t kMain[] = L"main.dll";
    constexpr size_t kLength = ( sizeof( kMain ) / sizeof( wchar_t ) ) - 1;
    if ( name == nullptr || name->buffer == nullptr || name->length != kLength * sizeof( wchar_t ) )
    {
        return false;
    }
    for ( size_t i = 0; i < kLength; ++i )
    {
        if ( std::towlower( name->buffer[ i ] ) != kMain[ i ] )
        {
            return false;
        }
    }
    return true;
}

void bootMain( HMODULE main );

// Runs under the loader lock once main.dll is mapped and before any of its code runs, as DllMain does.
VOID CALLBACK onDllLoaded( ULONG reason, const LoadedDll *dll, void * /*context*/ )
{
    if ( reason != kDllLoaded || !isMainDll( dll->baseName ) || mainSeen.exchange( true ) )
    {
        return;
    }
    wolf::writeLog( nullptr, WOLF_LOG_INFO, "main.dll loaded on thread %lu", GetCurrentThreadId() );
    bootMain( static_cast<HMODULE>( dll->base ) );
}

bool isModdedToken( const wchar_t *token, size_t length )
{
    constexpr wchar_t kFlag[] = L"-MODDED";
    constexpr size_t kLength = ( sizeof( kFlag ) / sizeof( wchar_t ) ) - 1;
    if ( length != kLength )
    {
        return false;
    }
    for ( size_t i = 0; i < kLength; ++i )
    {
        if ( std::towupper( token[ i ] ) != kFlag[ i ] )
        {
            return false;
        }
    }
    return true;
}

} // namespace

namespace wolf {

bool hasModdedFlag( const wchar_t *commandLine )
{
    // A token is a run of characters split by whitespace outside quotes. One holding a quote is a quoted argument, never the flag.
    bool quoted = false;
    bool hasQuote = false;
    const wchar_t *start = nullptr;
    for ( const wchar_t *at = commandLine;; ++at )
    {
        const wchar_t c = *at;
        const bool split = c == L'\0' || ( !quoted && ( c == L' ' || c == L'\t' ) );
        if ( split )
        {
            if ( start != nullptr && !hasQuote && isModdedToken( start, static_cast<size_t>( at - start ) ) )
            {
                return true;
            }
            start = nullptr;
            hasQuote = false;
            if ( c == L'\0' )
            {
                return false;
            }
            continue;
        }
        if ( start == nullptr )
        {
            start = at;
        }
        if ( c == L'"' )
        {
            quoted = !quoted;
            hasQuote = true;
        }
    }
}

bool readFingerprint( const void *image, Fingerprint *out )
{
    constexpr size_t kPage = 4096;
    const auto *base = static_cast<const uint8_t *>( image );
    IMAGE_DOS_HEADER dos{};
    std::memcpy( &dos, base, sizeof( dos ) );
    if ( dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || static_cast<size_t>( dos.e_lfanew ) > kPage - sizeof( IMAGE_NT_HEADERS64 ) )
    {
        return false;
    }
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy( &nt, base + dos.e_lfanew, sizeof( nt ) );
    if ( nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC )
    {
        return false;
    }
    out->timeDateStamp = nt.FileHeader.TimeDateStamp;
    out->sizeOfImage = nt.OptionalHeader.SizeOfImage;
    return true;
}

const wchar_t *gameDirectory()
{
    return gameDir;
}

void bootAttach( HMODULE self )
{
    HMODULE pinned = nullptr;
    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>( &bootAttach ), &pinned );
    if ( !hasModdedFlag( GetCommandLineW() ) )
    {
        return;
    }

    const DWORD length = GetModuleFileNameW( self, gameDir, MAX_PATH );
    if ( length == 0 || length == MAX_PATH )
    {
        return;
    }
    if ( wchar_t *slash = std::wcsrchr( gameDir, L'\\' ); slash != nullptr )
    {
        slash[ 1 ] = L'\0';
    }
    startFileLog( gameDir );
    writeLog( nullptr, WOLF_LOG_INFO, "WOLF %s attached on thread %lu", WOLF_VERSION_STRING, GetCurrentThreadId() );

    // The Steam overlay and other injectors can load dinput8.dll before the game loads main.dll, so WOLF also asks the loader to report main.dll. The
    // notification is registered before the check, so a main.dll that loads in between is not missed.
    const auto registerNotification =
        reinterpret_cast<RegisterDllNotification>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "LdrRegisterDllNotification" ) );
    const void *cookie = nullptr; // never unregistered: the notification stays for the process
    const bool notified = registerNotification != nullptr && registerNotification( 0, &onDllLoaded, nullptr, &cookie ) == 0;
    if ( const HMODULE main = GetModuleHandleW( L"main.dll" ); main != nullptr )
    {
        if ( !mainSeen.exchange( true ) )
        {
            bootMain( main );
        }
        return;
    }
    if ( notified )
    {
        writeLog( nullptr, WOLF_LOG_INFO, "main.dll is not loaded yet; WOLF hooks it when it loads" );
    }
    else
    {
        writeLog( nullptr, WOLF_LOG_ERROR, "main.dll is not loaded and the loader cannot report it; WOLF stays off" );
    }
}

} // namespace wolf

namespace {

// The rest of DllMain's work, on main.dll mapped at main, with the loader lock held.
void bootMain( HMODULE main )
{
    wolf::Fingerprint found{};
    if ( !wolf::readFingerprint( main, &found ) || found.timeDateStamp != wolf::kSupportedMain.timeDateStamp ||
         found.sizeOfImage != wolf::kSupportedMain.sizeOfImage )
    {
        snprintf( mismatch, sizeof( mismatch ), kMismatchFormat, found.timeDateStamp, found.sizeOfImage );
        wolf::writeLog( nullptr, WOLF_LOG_ERROR, "%s", mismatch );
        if ( const HANDLE thread = CreateThread( nullptr, 0, &showMismatch, nullptr, 0, nullptr ); thread != nullptr )
        {
            CloseHandle( thread );
        }
        return;
    }

    wolf::hooksInit();
    wolf::sitesInit();
    if ( !wolf::hooksReady() )
    {
        return;
    }
    wolf::hookFlowerStartup( main );
}

} // namespace
