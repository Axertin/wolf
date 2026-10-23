// The symbol table: every game function and global the runtime or a mod reaches by name. main.dll and flower_kernel.dll both have the preferred base
// 0x180000000, and the addresses in the comments below are VAs at that base; an RVA is the VA minus the base. main.dll entries hold an RVA, valid for the build
// the fingerprint gate admits. flower_kernel.dll entries hold the mangled export, whose address is an incremental-link thunk followed to the body. Resolution
// follows one E9 jump from the export, so an export pointing straight at a body that opens with a jmp rel32 would resolve to that jump's target; every export
// below is a thunk.

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
    // bool( bool warm ): false once before the first tick, true from flower_tick's soft-reset branch.
    mainSymbol( "flower_startup", 0x4b6240 ),
    // bool( void ).
    mainSymbol( "flower_tick", 0x4b63b0 ),
    // void( void ); flower_tick is its only caller.
    mainSymbol( "SoftReset_ExecuteAndReload", 0x4b5cd0 ),
    // void( void *sceneCtx ); runs on the map task worker for 85 to 197 frames.
    mainSymbol( "Room_Change", 0x494490 ),
    // void( void *itemMgr, int32_t itemId, int32_t delta ).
    mainSymbol( "Item_Add", 0x4965d0 ),
    // uint16_t, stage << 8 | room; already the new room when Room_Change is entered.
    mainSymbol( "g_currentRoomId", 0xb6b240 ),
    // void( uint32_t ref ), ref = group << 16 | bit; interlocked OR of 0x80000000 >> ( bit & 31 ) into the word at
    // block + 0x35c + group * 0x20 + ( bit >> 5 ) * 4, where block is *g_progressFlagBlockPtr.
    mainSymbol( "ProgressFlag_Set", 0x170830 ),
    // void( uint32_t ref ); the same word and mask as ProgressFlag_Set, cleared with an interlocked AND-NOT.
    mainSymbol( "ProgressFlag_Clear", 0x1707c0 ),
    // void *; null until GameState_BootInit, inside the cold flower_startup, points it at a static 0x11b0-byte block.
    mainSymbol( "g_progressFlagBlockPtr", 0xb205c0 ),
    // void *; the itemMgr argument the game passes to Item_Add.
    mainSymbol( "g_ItemMgrPtr", 0x7ab210 ),
    // void( cHeap *, void * ); the export at 0x180001e15 is a thunk to the body at 0x180040e50.
    kernelSymbol( "wk::mem::cHeap::free", "?free@cHeap@mem@wk@@UEAAXPEAX@Z" ),
    // void( cHeapExtended *, void * ); the export at 0x180001096 is a thunk to the body at 0x180040ee0.
    kernelSymbol( "wk::mem::cHeapExtended::free", "?free@cHeapExtended@mem@wk@@UEAAXPEAX@Z" ),
    // void( SwapChain *, unsigned ); the export at 0x18000237e is a thunk to the body at 0x18003e6a0.
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
