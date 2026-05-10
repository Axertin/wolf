#pragma once

#include <cstdint>
#include <string>

namespace wolf_tracer
{

// Initialize the symbol resolver: walks DAT_1807ad9b0 and seeds the
// address->label map. Idempotent. Safe to call after main.dll is loaded.
void initializeSymbolResolver();

// Register a label for an address (e.g. for our own hook target functions).
void addLabel(uintptr_t address, std::string label);

// Resolve an address to a human-readable label. Falls back to
// "main.dll+0xNNNNNN" if no label was registered. Never returns empty.
std::string resolveLabel(uintptr_t address);

} // namespace wolf_tracer
