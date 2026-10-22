// Turning the symbol table's names into addresses in the loaded game.

#pragma once

#include <windows.h>

#include <string_view>

namespace wolf {

// code + 5 + rel32 when code starts with an E9 jump, else code.
const void *followThunk( const void *code );

// Resolves every symbol: main.dll entries to main plus their RVA, kernel entries to their export followed past its thunk. Each that fails logs "symbol <name>
// did not resolve" and resolves to null. kernel is flower_kernel.dll; either may be null. Main thread at boot, before anything reads symbolAddress.
void symbolsResolve( HMODULE main, HMODULE kernel );

// The resolved address of name, or null for a name not in the table or not resolved. Any thread.
void *symbolAddress( std::string_view name );

} // namespace wolf
