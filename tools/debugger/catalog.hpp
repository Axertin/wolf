// The built-in probe catalog: every engine function and site the debugger can record. Every function here takes only integer or pointer arguments and returns
// in rax, so one detour shape forwards them all. main.dll and flower_kernel.dll both have the preferred base 0x180000000, and the comments give VAs at that
// base; rva is the VA minus the base. Kernel entries are function bodies, because the kernel's exports are incremental-link thunks.

#pragma once

#include <cstdint>
#include <iterator>
#include <string_view>
#include <utility>

namespace dbg {

enum class ProbeKind : uint8_t { Function, Completion, Site };
enum class ProbeModule : uint8_t { Main, Kernel, None };

struct ProbeArg
{
    uint8_t position; // among the integer arguments: rcx, rdx, r8, r9, then the stack
    const char *name; // null ends the list
};

struct CatalogEntry
{
    const char *name;
    ProbeKind kind;
    ProbeModule module;
    uint32_t rva;       // 0 for a symbol or site entry
    const char *symbol; // the WOLF symbol hooked by name, or the site; else null
    bool returns;
    int8_t textArg; // the recorded argument that is a C string, or -1
    ProbeArg args[ 4 ];
};

// clang-format off
// NOLINTBEGIN(modernize-use-designated-initializers): one positional row per entry reads as a table.
inline constexpr CatalogEntry kCatalog[] = {
    // 0x180041640 void *_MemoryAlloc( size, align, cHeap *&heap ).
    { "_MemoryAlloc", ProbeKind::Function, ProbeModule::Kernel, 0x41640, nullptr, true, -1, { { 0, "size" }, { 1, "align" }, { 2, "heap" } } },
    // 0x18001dcc0 void _MemoryFree( ptr ).
    { "_MemoryFree", ProbeKind::Function, ProbeModule::Kernel, 0x1dcc0, nullptr, false, -1, { { 0, "ptr" } } },
    // 0x18001dce0 void _MemoryFreeHeap( ptr, heap ).
    { "_MemoryFreeHeap", ProbeKind::Function, ProbeModule::Kernel, 0x1dce0, nullptr, false, -1, { { 0, "ptr" }, { 1, "heap" } } },
    // 0x180040980 void cHeap::clear( this ).
    { "cHeap::clear", ProbeKind::Function, ProbeModule::Kernel, 0x40980, nullptr, false, -1, { { 0, "heap" } } },
    // 0x180040a50 void cHeapExpandable::clear( this ).
    { "cHeapExpandable::clear", ProbeKind::Function, ProbeModule::Kernel, 0x40a50, nullptr, false, -1, { { 0, "heap" } } },
    // 0x180040a80 void cHeapExtended::clear( this ).
    { "cHeapExtended::clear", ProbeKind::Function, ProbeModule::Kernel, 0x40a80, nullptr, false, -1, { { 0, "heap" } } },
    // 0x180040ce0 void cHeap::destroy( this ); cHeapExtended shares it.
    { "cHeap::destroy", ProbeKind::Function, ProbeModule::Kernel, 0x40ce0, nullptr, false, -1, { { 0, "heap" } } },
    // 0x180040d80 void cHeapExpandable::destroy( this ).
    { "cHeapExpandable::destroy", ProbeKind::Function, ProbeModule::Kernel, 0x40d80, nullptr, false, -1, { { 0, "heap" } } },
    // 0x1801b0ad0 AllocRequest( mgr, path, dst, heap, id, explicitSize, userA, userB ), returns the request.
    { "AllocRequest", ProbeKind::Function, ProbeModule::Main, 0x1b0ad0, nullptr, true, 0, { { 1, "path" }, { 2, "dst" }, { 3, "heap" }, { 4, "id" } } },
    // 0x1801b07f0 CompleteRequest( mgr, out, decrypt, wait ); recorded from the head request it completes, so the arguments are the request's.
    { "CompleteRequest", ProbeKind::Completion, ProbeModule::Main, 0x1b07f0, nullptr, false, 0,
      { { 0, "path" }, { 1, "size" }, { 2, "buffer" }, { 3, "decrypt" } } },
    // 0x1801afc90 ReadAlloc( mgr, path, outBuf, heap, id ).
    { "ReadAlloc", ProbeKind::Function, ProbeModule::Main, 0x1afc90, nullptr, true, 0, { { 1, "path" }, { 2, "outBuf" }, { 3, "heap" }, { 4, "id" } } },
    // 0x1801afd20 ReadAllocDefaultHeap( mgr, path, outBuf, id ).
    { "ReadAllocDefaultHeap", ProbeKind::Function, ProbeModule::Main, 0x1afd20, nullptr, true, 0, { { 1, "path" }, { 2, "outBuf" }, { 3, "id" } } },
    // 0x1801afdc0 void ReadAllocDefaultHeapBlocking( mgr, path, outBuf, requestId ).
    { "ReadAllocDefaultHeapBlocking", ProbeKind::Function, ProbeModule::Main, 0x1afdc0, nullptr, false, 0,
      { { 1, "path" }, { 2, "outBuf" }, { 3, "requestId" } } },
    // 0x1801afe30 ReadAllocQuerySize( mgr, path, outBuf, heap, id, outSize ).
    { "ReadAllocQuerySize", ProbeKind::Function, ProbeModule::Main, 0x1afe30, nullptr, true, 0,
      { { 1, "path" }, { 2, "outBuf" }, { 3, "heap" }, { 5, "outSize" } } },
    // 0x1801afc20 ReadIntoCallerBuffer( mgr, path, dst, id, explicitSize ).
    { "ReadIntoCallerBuffer", ProbeKind::Function, ProbeModule::Main, 0x1afc20, nullptr, true, 0,
      { { 1, "path" }, { 2, "dst" }, { 3, "id" }, { 4, "explicitSize" } } },
    // 0x1803eb940 Resource_LoadToHeap( path, out, heap, mode ).
    { "Resource_LoadToHeap", ProbeKind::Function, ProbeModule::Main, 0x3eb940, nullptr, true, 0,
      { { 0, "path" }, { 1, "out" }, { 2, "heap" }, { 3, "mode" } } },
    // 0x180492fb0 ObjResRegistry_PreloadBootSet( ctx ).
    { "ObjResRegistry_PreloadBootSet", ProbeKind::Function, ProbeModule::Main, 0x492fb0, nullptr, true, -1, { { 0, "ctx" } } },
    // 0x180189ca0 ObjResRegistry_InsertPinned( reg, objId, data ).
    { "ObjResRegistry_InsertPinned", ProbeKind::Function, ProbeModule::Main, 0x189ca0, nullptr, true, -1, { { 0, "reg" }, { 1, "objId" }, { 2, "data" } } },
    // 0x180189430 ObjResRegistry_Preload( reg, objId, sizeOut ).
    { "ObjResRegistry_Preload", ProbeKind::Function, ProbeModule::Main, 0x189430, nullptr, true, -1, { { 0, "reg" }, { 1, "objId" }, { 2, "sizeOut" } } },
    // 0x180189650 ObjResRegistry_Acquire( reg, objId, heap, addRefElsePin ).
    { "ObjResRegistry_Acquire", ProbeKind::Function, ProbeModule::Main, 0x189650, nullptr, true, -1,
      { { 0, "reg" }, { 1, "objId" }, { 2, "heap" }, { 3, "addRefElsePin" } } },
    // 0x180188c50 ObjResRegistry_AcquireResident( reg, objId ).
    { "ObjResRegistry_AcquireResident", ProbeKind::Function, ProbeModule::Main, 0x188c50, nullptr, true, -1, { { 0, "reg" }, { 1, "objId" } } },
    // 0x18018a1e0 void ObjResRegistry_ForceRelease( reg, objId, zeroRefCount ).
    { "ObjResRegistry_ForceRelease", ProbeKind::Function, ProbeModule::Main, 0x18a1e0, nullptr, false, -1,
      { { 0, "reg" }, { 1, "objId" }, { 2, "zeroRefCount" } } },
    // 0x180189170 ObjResRegistry_IsResident( reg, objId ).
    { "ObjResRegistry_IsResident", ProbeKind::Function, ProbeModule::Main, 0x189170, nullptr, true, -1, { { 0, "reg" }, { 1, "objId" } } },
    // 0x1801889b0 ObjResSlot_RegisterWithMemTracker( slot, objId, category ).
    { "ObjResSlot_RegisterWithMemTracker", ProbeKind::Function, ProbeModule::Main, 0x1889b0, nullptr, true, -1,
      { { 0, "slot" }, { 1, "objId" }, { 2, "category" } } },
    // 0x180188bf0 void ObjResSlot_FreeAndMarkDestroyed( slot ).
    { "ObjResSlot_FreeAndMarkDestroyed", ProbeKind::Function, ProbeModule::Main, 0x188bf0, nullptr, false, -1, { { 0, "slot" } } },
    // 0x180140ec0 void ResourceFixup_WalkContainer( ctx, category, container, depth, flags ).
    { "ResourceFixup_WalkContainer", ProbeKind::Function, ProbeModule::Main, 0x140ec0, nullptr, false, -1,
      { { 1, "category" }, { 2, "container" }, { 3, "depth" }, { 4, "flags" } } },
    // 0x180140970 void ResourceFixup_TextureChunk( ctx, container, slot, noDecay ).
    { "ResourceFixup_TextureChunk", ProbeKind::Function, ProbeModule::Main, 0x140970, nullptr, false, -1,
      { { 1, "container" }, { 2, "slot" }, { 3, "noDecay" } } },
    // 0x180141620 GXTextureManager_AllocSlotAndLoadDDS( mgr, dds, noDecay ).
    { "GXTextureManager_AllocSlotAndLoadDDS", ProbeKind::Function, ProbeModule::Main, 0x141620, nullptr, true, -1, { { 1, "dds" }, { 2, "noDecay" } } },
    // 0x180140b20 void GXTextureManager_PostFixupJob( mgr, mode, container ).
    { "GXTextureManager_PostFixupJob", ProbeKind::Function, ProbeModule::Main, 0x140b20, nullptr, false, -1, { { 1, "mode" }, { 2, "container" } } },
    // 0x180140bf0 void GXTextureManager_FreeCategory( mgr, category ).
    { "GXTextureManager_FreeCategory", ProbeKind::Function, ProbeModule::Main, 0x140bf0, nullptr, false, -1, { { 1, "category" } } },
    // 0x180140cb0 void GXTextureManager_FreeAll( mgr ).
    { "GXTextureManager_FreeAll", ProbeKind::Function, ProbeModule::Main, 0x140cb0, nullptr, false, -1, {} },
    // 0x180140db0 void GXTextureManager_FreeAllButCategory0( mgr ).
    { "GXTextureManager_FreeAllButCategory0", ProbeKind::Function, ProbeModule::Main, 0x140db0, nullptr, false, -1, {} },
    // 0x1801c9510 void MsgResource_LoadCoreMsdSlot( msg ).
    { "MsgResource_LoadCoreMsdSlot", ProbeKind::Function, ProbeModule::Main, 0x1c9510, nullptr, false, -1, { { 0, "msg" } } },
    // 0x1801c92c0 void MsgResource_LoadMsdSlot( msg ).
    { "MsgResource_LoadMsdSlot", ProbeKind::Function, ProbeModule::Main, 0x1c92c0, nullptr, false, -1, { { 0, "msg" } } },
    // 0x180417cf0 void cSubScrBase_BeginScreenResourceLoad( screen, path ).
    { "cSubScrBase_BeginScreenResourceLoad", ProbeKind::Function, ProbeModule::Main, 0x417cf0, nullptr, false, 1, { { 0, "screen" }, { 1, "path" } } },
    // 0x180417bf0 void cSubScrBase_ReleaseScreenResource( screen ).
    { "cSubScrBase_ReleaseScreenResource", ProbeKind::Function, ProbeModule::Main, 0x417bf0, nullptr, false, -1, { { 0, "screen" } } },
    // 0x1804b5cd0 void SoftReset_ExecuteAndReload( void ).
    { "SoftReset_ExecuteAndReload", ProbeKind::Function, ProbeModule::Main, 0, "SoftReset_ExecuteAndReload", false, -1, {} },
    // WOLF's room.change site: the room ids a change goes from and to.
    { "room.change", ProbeKind::Site, ProbeModule::None, 0, "room.change", false, -1, { { 0, "from" }, { 1, "to" } } },
};
// NOLINTEND(modernize-use-designated-initializers)
// clang-format on

// The preferred base of main.dll and flower_kernel.dll, which the VAs in comments and configs assume.
inline constexpr uint64_t kPreferredBase = 0x180000000;

inline constexpr uint16_t kCatalogSize = std::size( kCatalog );

constexpr int32_t catalogFind( std::string_view name )
{
    for ( uint16_t i = 0; i < kCatalogSize; ++i )
    {
        if ( name == kCatalog[ i ].name )
        {
            return i;
        }
    }
    return -1;
}

constexpr uint8_t catalogArgCount( const CatalogEntry &entry )
{
    uint8_t n = 0;
    while ( n < 4 && entry.args[ n ].name != nullptr )
    {
        ++n;
    }
    return n;
}

// Names are unique, positions are among the 8 integer arguments a detour forwards, the text argument is a recorded one, and only functions return.
constexpr bool catalogWellFormed()
{
    for ( uint16_t i = 0; i < kCatalogSize; ++i )
    {
        const CatalogEntry &e = kCatalog[ i ];
        if ( catalogFind( e.name ) != i || std::cmp_greater_equal( e.textArg, catalogArgCount( e ) ) || ( e.returns && e.kind != ProbeKind::Function ) )
        {
            return false;
        }
        for ( uint8_t a = 0; a < catalogArgCount( e ); ++a )
        {
            if ( e.args[ a ].position >= 8 )
            {
                return false;
            }
        }
    }
    return true;
}
static_assert( catalogWellFormed() );

} // namespace dbg
