// The game-test harness, and doctest's main for wolf-gametests.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "harness.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <doctest/doctest.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace harness {

std::wstring binDir()
{
    wchar_t path[ MAX_PATH ];
    const DWORD length = GetModuleFileNameW( nullptr, path, MAX_PATH );
    const std::wstring dir( path, length );
    return dir.substr( 0, dir.find_last_of( L'\\' ) + 1 );
}

std::wstring systemDinput8Path()
{
    wchar_t dir[ MAX_PATH ];
    const UINT length = GetSystemDirectoryW( dir, MAX_PATH );
    return std::wstring( dir, length ) + L"\\dinput8.dll";
}

namespace {

// Copies fake_main.dll to main.dll with the real main.dll's TimeDateStamp and SizeOfImage. The last section's virtual size grows to reach the new SizeOfImage,
// so the header stays consistent and the loader maps the whole range as zero-filled memory.
bool writeFingerprintedMain()
{
    std::ifstream in( ( binDir() + L"fake_main.dll" ).c_str(), std::ios::binary );
    std::vector<char> image{ std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
    if ( image.size() < sizeof( IMAGE_DOS_HEADER ) )
    {
        return false;
    }

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>( image.data() );
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>( image.data() + dos->e_lfanew );
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION( nt );
    IMAGE_SECTION_HEADER &last = sections[ nt->FileHeader.NumberOfSections - 1 ];
    if ( last.VirtualAddress >= kMainSizeOfImage )
    {
        return false;
    }
    last.Misc.VirtualSize = kMainSizeOfImage - last.VirtualAddress;
    nt->OptionalHeader.SizeOfImage = kMainSizeOfImage;
    nt->FileHeader.TimeDateStamp = kMainTimeDateStamp;

    std::ofstream out( ( binDir() + L"main.dll" ).c_str(), std::ios::binary | std::ios::trunc );
    out.write( image.data(), static_cast<std::streamsize>( image.size() ) );
    return out.good();
}

// Makes the proxy's first kFirstCalls DirectInput8Create calls from that many threads released together, and counts the ones that returned an interface.
int raceFirstCalls()
{
    using CreateFn = HRESULT( WINAPI * )( HINSTANCE, DWORD, REFIID, void **, IUnknown * );
    const auto create = reinterpret_cast<CreateFn>( GetProcAddress( GetModuleHandleW( ( binDir() + L"dinput8.dll" ).c_str() ), "DirectInput8Create" ) );
    if ( create == nullptr )
    {
        return 0;
    }
    std::atomic<int> ready{ 0 };
    std::atomic<int> created{ 0 };
    std::vector<std::thread> threads;
    threads.reserve( kFirstCalls );
    for ( int i = 0; i < kFirstCalls; ++i )
    {
        threads.emplace_back( [ & ] {
            ready.fetch_add( 1 );
            while ( ready.load() < kFirstCalls )
            {
                std::this_thread::yield();
            }
            IDirectInput8W *input = nullptr;
            if ( create( GetModuleHandleW( nullptr ), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>( &input ), nullptr ) == DI_OK &&
                 input != nullptr )
            {
                input->Release();
                created.fetch_add( 1 );
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }
    return created.load();
}

} // namespace

const FakeMain &fakeMain()
{
    static const FakeMain loaded = [] {
        FakeMain result;
        result.proxyLoadedBefore = GetModuleHandleW( L"dinput8.dll" ) != nullptr;
        if ( writeFingerprintedMain() )
        {
            result.main = LoadLibraryW( ( binDir() + L"main.dll" ).c_str() );
        }
        result.systemLoadedOnLoad = GetModuleHandleW( systemDinput8Path().c_str() ) != nullptr;
        if ( result.main != nullptr )
        {
            result.firstCallsCreated = raceFirstCalls();
        }
        return result;
    }();
    return loaded;
}

} // namespace harness
