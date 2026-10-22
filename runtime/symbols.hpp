// The symbol table: every game function and global the runtime or a mod reaches by name. Values come from the okami-kb store, whose keys are VAs at the
// preferred base 0x180000000 of both main.dll and flower_kernel.dll; an RVA here is that VA minus the base. main.dll entries hold an RVA, valid for the build
// the fingerprint gate admits. flower_kernel.dll entries hold the mangled export, whose address is an incremental-link thunk followed to the body. Resolution
// follows one E9 jump from the export, so an export pointing straight at a body that opens with a jmp rel32 would resolve to that jump's target; every export
// below is a thunk in the KB.

#pragma once

#include <cstdint>
#include <string_view>

namespace wolf {

enum class SymbolModule : uint8_t { Main, Kernel };

struct Symbol
{
    const char *name; // what mods pass to hook and resolve
    SymbolModule module;
    uint32_t rva;           // main.dll entries; 0 for kernel entries, which resolve by export
    const char *exportName; // kernel entries: the mangled export; null for main.dll entries
};

constexpr Symbol mainSymbol( const char *name, uint32_t rva )
{
    return { .name = name, .module = SymbolModule::Main, .rva = rva, .exportName = nullptr };
}

constexpr Symbol kernelSymbol( const char *name, const char *exportName )
{
    return { .name = name, .module = SymbolModule::Kernel, .rva = 0, .exportName = exportName };
}

inline constexpr Symbol kSymbols[] = {
    // KB 0x1804b6240, confirmed, read 2026-10-17. bool( bool warm ): false once before the first tick, true from flower_tick's soft-reset branch.
    mainSymbol( "flower_startup", 0x4b6240 ),
    // KB 0x1804b63b0, confirmed, read 2026-10-17. bool( void ).
    mainSymbol( "flower_tick", 0x4b63b0 ),
    // KB 0x1804b5cd0, confirmed, read 2026-10-17. void( void ); flower_tick is its only caller.
    mainSymbol( "SoftReset_ExecuteAndReload", 0x4b5cd0 ),
    // KB 0x180494490, inferred, conflict flag set, read 2026-10-17. void( void *sceneCtx ); runs on the map task worker for 85 to 197 frames.
    mainSymbol( "Room_Change", 0x494490 ),
    // KB 0x1804965d0, inferred, read 2026-10-17. void( void *itemMgr, int32_t itemId, int32_t delta ).
    mainSymbol( "Item_Add", 0x4965d0 ),
    // KB 0x180b6b240, confirmed, read 2026-10-17. uint16_t, stage << 8 | room; already the new room when Room_Change is entered.
    mainSymbol( "g_currentRoomId", 0xb6b240 ),
    // KB body 0x180040e50, confirmed, conflict flag set, read 2026-10-17. void( cHeap *, void * ); the export is a thunk at 0x180001e15.
    kernelSymbol( "wk::mem::cHeap::free", "?free@cHeap@mem@wk@@UEAAXPEAX@Z" ),
    // KB body 0x180040ee0, confirmed, read 2026-10-17. void( cHeapExtended *, void * ); the export is a thunk at 0x180001096.
    kernelSymbol( "wk::mem::cHeapExtended::free", "?free@cHeapExtended@mem@wk@@UEAAXPEAX@Z" ),
    // KB body 0x18003e6a0, confirmed, read 2026-10-17. void( SwapChain *, unsigned ); the export is a thunk at 0x18000237e.
    kernelSymbol( "m2::render::SwapChain::swap", "?swap@SwapChain@render@m2@@QEAAXI@Z" ),
};

// The main.dll RVA of name; 0 when it is absent or a kernel entry.
constexpr uint32_t symbolRva( std::string_view name )
{
    for ( const Symbol &symbol : kSymbols )
    {
        if ( symbol.name == name )
        {
            return symbol.rva;
        }
    }
    return 0;
}

} // namespace wolf
