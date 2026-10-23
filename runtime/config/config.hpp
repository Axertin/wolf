// Per-mod config read from ini text.

#pragma once

#include <cstdint>
#include <string_view>

#include "mods/mod.hpp"

namespace wolf {

// Main thread, boot: appends text's pairs to the config table for mod, keys in a [section] stored as section.key, and sets mod->firstConfig and configCount.
// Called once per mod, before the next mod's call. A malformed line logs "<label> line <n> has no key = value; skipped" naming mod. Takes the runtime lock.
void configParse( WolfMod *mod, std::string_view text, const char *label );

// Readable from any thread: entries are appended only during boot, before any reader exists.
const char *configValue( WolfMod *mod, const char *key ); // the last line's value for key, or null
int64_t configInt( WolfMod *mod, const char *key, int64_t fallback );
double configFloat( WolfMod *mod, const char *key, double fallback );  // decimal over the whole value, any locale; else fallback
int32_t configBool( WolfMod *mod, const char *key, int32_t fallback ); // 1 for 1/true/yes/on, 0 for 0/false/no/off, any case; else fallback
const char *configStr( WolfMod *mod, const char *key, const char *fallback );

} // namespace wolf
