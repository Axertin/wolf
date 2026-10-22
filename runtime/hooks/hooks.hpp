// Hook chains: one MinHook head per hooked function, with mod detours linked inside it in load order.
//
// Every target has a 16-byte stub, jmp [rip+2] then an 8-byte entry slot holding the outermost link's detour, or MinHook's trampoline when nothing is linked.
// A plain target's head jumps to its stub; a site target's head is the runtime's site detour, which calls the stub. Linking and withdrawing are aligned 8-byte
// stores into the entry slot or a link's *original, made at the frame boundary, so the game can walk a chain from any thread. Targets, links and stubs are
// never freed, and a detour or original once used on one target is never used on another, so a thread parked inside a withdrawn link returns through it.

#pragma once

#include <wolf/wolf.h>

#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

// Main thread, before any other call here: initialises MinHook and the hook registry. Later calls return at once. When MinHook fails it logs
// "MinHook did not initialise: <status>" and every head fails afterwards.
void hooksInit();

bool hooksReady(); // MinHook initialised

// Any thread. Queues a link of detour on the function at target for the next frame boundary, which writes *original before the link becomes reachable, and
// registryRemove withdraws it at the boundary after. original is 8-byte aligned, which the C table checks. Chains run outermost first, ordered by the mod's
// load index and then by when the link was first added, later further out. 0 when refused, which logs: a detour or original tied to another hook, or this hook
// already linked. 0 when a table is full or out of memory. A removed hook added again revives at its position. Takes the runtime lock.
WolfHandle hookAdd( WolfMod *mod, void *target, void *detour, void **original );

// Main thread, boot. Makes siteDetour the head of target and returns what siteDetour calls to run the chain inside it. The head is enabled by the next
// hooksCommit. Null when MinHook refuses target, which logs. Takes the runtime lock.
void *hookSite( void *target, void *siteDetour );

// Main thread, with the runtime lock free: enables every head created since the last call in one MinHook batch, which suspends the other threads once.
void hooksCommit();

// rva bytes into the loaded module named module, or null when it is not loaded or rva is at or past its SizeOfImage. Any thread.
void *rawTarget( const char *module, uintptr_t rva );

} // namespace wolf
