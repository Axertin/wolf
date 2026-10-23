// Mods found on disk, in load order.

#pragma once

#include <cstdint>

#include "mods/mod.hpp"

namespace wolf {

struct ModList
{
    WolfMod **mods = nullptr; // load order; the records live in the mods table for the process
    uint32_t count = 0;
};

// Main thread, boot: one record per directory under <gameDir>mods\, in load order with loadIndex set. gameDir ends in a backslash. Each file directly under
// mods\ logs "mods: skipping stray file <name>". A missing mods directory logs "mods: no mods directory" and returns an empty list. A mod directory whose path
// does not fit 1024 wide characters logs "mods: path of <id> is too long; skipped". mod.ini sets name, version and priority, config.ini goes to configParse
// with the label "mods/<id>/config.ini", and dllPath is set when <id>.dll exists. Files over 1 MiB are ignored. Logs one summary line per mod in load order.
// Takes the runtime lock in short holds.
ModList modsDiscover( const wchar_t *gameDir );

// Main thread, boot: sets mod->refused and logs "mod <id> refused: <reason>; its registrations, assets and config are dropped" naming mod. Runs only before
// the apply that follows the inits, so none of a refused mod's records ever becomes live: applyPending frees them, and dispatch needs no refused check. A
// refusal after that apply would leave live records that dispatch still runs.
void modRefuse( WolfMod *mod, const char *reason );

// Main thread, cold flower_startup: for each code mod in list order, LoadLibraryExW( dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH ), so the DLL's own
// directory is searched for the DLLs it imports, then its wolf_mod_init export called with api, both inside a DispatchScope for the mod. Never frees a DLL.
// Refusals, through modRefuse: "its DLL did not load (error <GetLastError>)", "its DLL exports no wolf_mod_init", "wolf_mod_init returned <n>".
void modsLoad( const ModList &list, const WolfApi *api );

} // namespace wolf
