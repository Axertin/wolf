# DevTools Migration Context Summary

This document contains key references and context for the DevTools WOLF mod migration project.

## Project Overview

Successfully migrated DevTools from unified kitchen sink mod to independent WOLF-based mod. The migration is complete through **Phase 2** with basic functionality working.

## Current Status

✅ **Phases 1-2 Complete**: Infrastructure setup, core framework migration, basic GUI functionality
🔄 **Next**: Can expand with additional features (inventory, brushes, maps, collections)

## Key Files Currently Active

### Core Implementation
- **`src/devtools/devtools.cpp`**: Main WOLF mod entry point using `WOLF_MOD_ENTRY_CLASS(DevToolsMod)`
- **`src/devtools/devtools_gui.cpp`**: Expanded GUI with character stats, cheats, global flags
- **`src/devtools/devtools_gui.h`**: GUI function declarations
- **`src/devtools/memory_accessors.h`**: Centralized memory accessors using WOLF's `MemoryAccessor<T>`

### Supporting Systems
- **`src/devtools/devdatafinder.cpp/.h`**: BitField monitoring using WOLF's `createBitfieldMonitor()`
- **`src/devtools/gamestateregistry.cpp/.h/.igamestateregistry.h`**: YAML-based game state documentation
- **`src/devtools/frametimer.h`**: Performance timing utility
- **`src/devtools/game-data/`**: Comprehensive YAML configuration (80+ map files, schemas)

### Build System
- **`src/devtools/CMakeLists.txt`**: Optional build target (`-DBUILD_DEVTOOLS=ON`)
- **`src/devtools/migration.md`**: Detailed migration plan (phases 1-10)
- Use the x64-clang-debug preset

## Memory Architecture

**WOLF MemoryAccessors in `memory_accessors.h`:**
```cpp
// Offsets from main.dll base (from unified mod memorymap.cpp)
constexpr uintptr_t AMMY_STATS_OFFSET = 0xB4DF90;
constexpr uintptr_t AMMY_COLLECTIONS_OFFSET = 0xB205D0;
// ... etc

// Accessors
wolf::MemoryAccessor<okami::CharacterStats> AmmyStats;
wolf::MemoryAccessor<okami::CollectionData> AmmyCollections;
// ... etc
```

## WOLF Framework Integration

**Key APIs Used:**
- **Entry**: `WOLF_MOD_ENTRY_CLASS(DevToolsMod)` in `wolf_core.hpp`
- **GUI**: `wolf::registerGuiWindow()` in `wolf_gui.hpp`
- **Memory**: `wolf::MemoryAccessor<T>` in `wolf_memory.hpp`
- **BitField**: `wolf::createBitfieldMonitor()` in `wolf_bitfield.hpp`
- **Logging**: `wolf::logInfo()`, `wolf::logError()` etc.

## Current GUI Features

### Working Functionality
- **CHEAT ME Button**: Unlocks brushes, gives items, sets money (mirrors original)
- **Character Stats**: Health, Money, Praise, Ink, Food, Godhood editing
- **Global Game State**: 86 flags with GameStateRegistry descriptions
- **Performance**: Frame time, FPS, In-Game Timer
- **Runtime Info**: WOLF version, build info

### Architecture Pattern
```cpp
// Memory access pattern
auto* ammyStats = AmmyStats.get_ptr();
if (ammyStats) {
    drawStatPair("Health", ImGuiDataType_U16, &ammyStats->currentHealth, &ammyStats->maxHealth);
}

// BitField editing with registry
auto& registry = GameStateRegistry::instance();
auto& globalDesc = registry.getGlobalConfig().globalGameState;
checkboxBitField(globalDesc.at(i).c_str(), i, *globalFlags);
```

## Reference Implementation

**Original unified mod DevTools**: `C:\Users\axertin\Documents\okami-apclient\src\client\devtools.cpp`
- 800+ lines with comprehensive game editing
- Inventory, weapons, maps, collections, tracker data
- Pattern for expansion: helper functions, template-based BitField UI
`c:/Users/axertin/Documents/okami-apclient/include/okami/memorymap.hpp`
- Original memory offset mapping, reference for things that might have been missed in `include/okami/offsets.hpp`

## Key Dependencies

**Okami Headers (`include/okami/`):**
- `structs.hpp`: Game data structures (`CharacterStats`, `CollectionData`, etc.)
- `itemtype.hpp`: Item type enums and names
- `maptype.hpp`: Map type enums and `GetName()` function
- `bitfield.hpp`: `BitField<N>` template class

**WOLF Framework (`src/api/`):**
- `wolf_framework.hpp`: Generated unified header
- Individual headers: `wolf_core.hpp`, `wolf_memory.hpp`, `wolf_gui.hpp`, `wolf_bitfield.hpp`

## Build Status

✅ **Compiles Successfully**: `cmake --build build/x64-debug --target devtools`
✅ **Optional Target**: Only builds with `-DBUILD_DEVTOOLS=ON`
✅ **Independent**: No unified mod dependencies
✅ **Install Target**: Installs to `mods/devtools/` with game-data YAML files

## Expansion Roadmap

**Ready to Add** (following original patterns):
1. **Inventory Management**: Item categories, quantities
2. **Brush System**: Usable/obtained brushes, upgrades
3. **Map System**: Teleportation, current map editing
4. **Collections**: Stray beads, travel guides, animal tomes
5. **World State**: Time of day, map state bits per location

**Helper Functions Available:**
- `drawStat()`, `drawStatPair()`: Stat editing
- `checkboxBitField<N>()`: BitField checkbox with tooltip
- `IndentedGroup`: RAII collapsing headers
- `GROUP(name)` macro: Convenient grouping

All infrastructure is in place for rapid feature expansion using the established patterns.