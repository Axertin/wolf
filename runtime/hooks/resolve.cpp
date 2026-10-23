#include "hooks/resolve.hpp"

#include <cstdint>
#include <cstring>
#include <iterator>

#include "log/log.hpp"
#include "symbols.hpp"

namespace {

// Parallel to kSymbols. Written only by symbolsResolve, which runs before any reader exists.
void *addresses[ std::size( wolf::kSymbols ) ] = {};

} // namespace

namespace wolf {

const void *followThunk( const void *code )
{
    const auto *bytes = static_cast<const uint8_t *>( code );
    if ( bytes[ 0 ] != 0xE9 )
    {
        return code;
    }
    int32_t rel = 0;
    std::memcpy( &rel, bytes + 1, sizeof( rel ) );
    return bytes + 5 + rel;
}

void symbolsResolve( HMODULE main, HMODULE kernel )
{
    for ( size_t i = 0; i < std::size( kSymbols ); ++i )
    {
        const Symbol &symbol = kSymbols[ i ];
        void *address = nullptr;
        if ( symbol.module == SymbolModule::Main )
        {
            if ( main != nullptr )
            {
                address = reinterpret_cast<uint8_t *>( main ) + symbol.rva;
            }
        }
        else if ( kernel != nullptr )
        {
            if ( const FARPROC exported = GetProcAddress( kernel, symbol.exportName ); exported != nullptr )
            {
                address = const_cast<void *>( followThunk( reinterpret_cast<const void *>( exported ) ) );
            }
        }
        if ( address == nullptr )
        {
            writeLog( nullptr, WOLF_LOG_WARN, "symbol %s did not resolve", symbol.name );
        }
        addresses[ i ] = address;
    }
}

void *symbolAddress( std::string_view name )
{
    for ( size_t i = 0; i < std::size( kSymbols ); ++i )
    {
        if ( kSymbols[ i ].name == name )
        {
            return addresses[ i ];
        }
    }
    return nullptr;
}

const char *moduleName( const void *address, char ( &path )[ MAX_PATH ], uintptr_t *base )
{
    HMODULE module = nullptr;
    if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>( address ), &module ) ==
             0 ||
         GetModuleFileNameA( module, path, MAX_PATH ) == 0 )
    {
        return nullptr;
    }
    *base = reinterpret_cast<uintptr_t>( module );
    const char *slash = std::strrchr( path, '\\' );
    return slash != nullptr ? slash + 1 : path;
}

} // namespace wolf
