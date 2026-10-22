// The game-test harness, and doctest's main for wolf-gametests.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "harness.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <symbols.hpp>
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

// Copies fake_main.dll to main.dll with the real main.dll's TimeDateStamp and SizeOfImage, and appends a section .game that runs from the end of the image to
// SizeOfImage. At each main.dll function RVA of the symbol table it holds an E9 jump to fake_main.dll's export Fake_<name>; a symbol with no such export is
// data, and its bytes stay zero.
bool writeFingerprintedMain()
{
    const std::wstring fakePath = binDir() + L"fake_main.dll";
    std::ifstream in( fakePath.c_str(), std::ios::binary );
    std::vector<char> image{ std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
    if ( image.size() < sizeof( IMAGE_DOS_HEADER ) )
    {
        return false;
    }

    // The export RVAs, read from a mapping that resolves no imports, so dinput8.dll stays unloaded.
    struct Jump
    {
        uint32_t from = 0;
        uint32_t to = 0;
    };
    std::vector<Jump> jumps;
    const HMODULE mapped = LoadLibraryExW( fakePath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES );
    if ( mapped == nullptr )
    {
        return false;
    }
    for ( const wolf::Symbol &symbol : wolf::kSymbols )
    {
        if ( symbol.module != wolf::SymbolModule::Main )
        {
            continue;
        }
        const std::string exportName = std::string( "Fake_" ) + symbol.name;
        if ( const FARPROC target = GetProcAddress( mapped, exportName.c_str() ); target != nullptr )
        {
            jumps.push_back(
                { .from = symbol.rva, .to = static_cast<uint32_t>( reinterpret_cast<uint8_t *>( target ) - reinterpret_cast<uint8_t *>( mapped ) ) } );
        }
    }
    FreeLibrary( mapped );

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>( image.data() );
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>( image.data() + dos->e_lfanew );
    const uint32_t fileAlign = nt->OptionalHeader.FileAlignment;
    const uint32_t sectionAlign = nt->OptionalHeader.SectionAlignment;
    const auto alignUp = []( uint32_t value, uint32_t to ) { return ( value + to - 1 ) / to * to; };

    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION( nt );
    const WORD count = nt->FileHeader.NumberOfSections;
    const auto headerEnd = static_cast<uint32_t>( reinterpret_cast<char *>( sections + count + 1 ) - image.data() );
    if ( headerEnd > nt->OptionalHeader.SizeOfHeaders )
    {
        return false;
    }
    const IMAGE_SECTION_HEADER &last = sections[ count - 1 ];
    IMAGE_SECTION_HEADER game{};
    std::memcpy( game.Name, ".game", 5 );
    game.VirtualAddress = alignUp( last.VirtualAddress + std::max<uint32_t>( last.Misc.VirtualSize, last.SizeOfRawData ), sectionAlign );
    game.Misc.VirtualSize = kMainSizeOfImage - game.VirtualAddress;
    uint32_t rawEnd = game.VirtualAddress;
    for ( const Jump &jump : jumps )
    {
        if ( jump.from < game.VirtualAddress || jump.from + 5 > kMainSizeOfImage )
        {
            return false;
        }
        rawEnd = std::max( rawEnd, jump.from + 16 );
    }
    game.SizeOfRawData = alignUp( rawEnd - game.VirtualAddress, fileAlign );
    game.PointerToRawData = alignUp( static_cast<uint32_t>( image.size() ), fileAlign );
    game.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    sections[ count ] = game;
    nt->FileHeader.NumberOfSections = static_cast<WORD>( count + 1 );
    nt->OptionalHeader.SizeOfImage = kMainSizeOfImage;
    nt->FileHeader.TimeDateStamp = kMainTimeDateStamp;

    image.resize( game.PointerToRawData + game.SizeOfRawData, 0 );
    for ( const Jump &jump : jumps )
    {
        char *at = image.data() + game.PointerToRawData + ( jump.from - game.VirtualAddress );
        const auto rel = static_cast<int32_t>( static_cast<int64_t>( jump.to ) - ( static_cast<int64_t>( jump.from ) + 5 ) );
        at[ 0 ] = static_cast<char>( 0xE9 );
        std::memcpy( at + 1, &rel, sizeof( rel ) );
    }

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
        std::error_code ignored;
        std::filesystem::remove_all( binDir() + L"logs", ignored );
        // An overlay or injector can load dinput8.dll before the game loads main.dll; WOLF_GAMETESTS_PROXY_FIRST=1 makes this process do the same.
        if ( GetEnvironmentVariableW( L"WOLF_GAMETESTS_PROXY_FIRST", nullptr, 0 ) != 0 )
        {
            LoadLibraryW( ( binDir() + L"dinput8.dll" ).c_str() );
        }
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

std::string wolfLog()
{
    std::ifstream in( ( binDir() + L"logs\\wolf.log" ).c_str(), std::ios::binary );
    return { std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
}

} // namespace harness
