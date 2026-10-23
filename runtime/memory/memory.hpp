// Game memory for mods: write copies at once, a patch is written at the frame boundary and undone on remove, and a watch compares its bytes with a snapshot
// at each frame boundary.

#pragma once

#include <wolf/wolf.h>

#include <cstddef>
#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

// Main thread, before any other call here: installs the patch and watch registries. Later calls return at once.
void memoryInit();

// Any thread: true when VirtualQuery, walked region by region, shows every region [addr, addr + n) touches committed and not PAGE_NOACCESS, PAGE_GUARD or
// execute-only PAGE_EXECUTE. The C table checks every patch and watch range with it, and logs a range it refuses.
bool memoryReadable( const void *addr, size_t n );

// Any thread; takes the runtime lock. Copies n bytes from src to addr when memoryReadable( addr, n ), checked under the lock so no other write changes the
// pages' protection between the check and the copy. Each region the range touches is written on its own: a read-only or executable region gets write access
// for the copy and its protection back after, and executable memory has its instruction cache flushed. 1 when written. 0 when the range is not readable
// memory, or when a region fails after the check, which can leave the regions before it written. The C table checks addr, and src for a nonzero n.
int32_t memoryWrite( void *addr, const void *src, size_t n );

// Any thread. In one hold of the runtime lock, copies src into the lifetime's arena and queues the patch. The boundary that applies it checks the range again,
// saves the bytes at addr and writes src's; a patch overlapping a live patch, or one that cannot be written, is not applied, leaves the memory as it was and
// logs naming its mod. On remove the saved bytes go back. When its lifetime ends, image memory (every region MEM_IMAGE at registration) gets them back too,
// and other memory keeps what the game rebuilt there. 0 for a full registry or arena. lifetime is process, session or room; the C table checks it, addr, src
// for a nonzero n, and the range.
WolfHandle patchAdd( WolfMod *mod, void *addr, const void *src, size_t n, int32_t lifetime );

// Any thread. In one hold of the runtime lock, allocates the snapshot in the lifetime's arena and queues the watch; the boundary that applies it checks the
// range again and takes the snapshot. 0 for a full registry or arena. A watch whose range is no longer readable when the boundary applies it logs a warning
// naming its mod and never fires. lifetime is process, session or room; the C table checks it, addr, cb, n and the range.
WolfHandle watchAdd( WolfMod *mod, const void *addr, size_t n, WolfCallback cb, void *user, int32_t lifetime );

// Main thread, frame boundary, after runQueued: each live watch compares its bytes with its snapshot, and when they differ calls cb( mod, &ctx, user ), then
// copies the bytes into the snapshot. The compare, the call and the copy all run as the mod's dispatch.
void watchesCompare();

} // namespace wolf
