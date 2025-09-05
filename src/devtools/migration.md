# DevTools Migration Plan: Unified Mod to Independent WOLF Mod

## Overview

This document outlines the comprehensive migration strategy for transforming the trainer and game investigation tools from the unified kitchen sink mod to an independent devtools mod in the WOLF repository.

## Current Situation Analysis

### Existing Code Structure
- **devdatafinder.cpp/.h**: Contains 80+ BitFieldChangeDetector instances for monitoring various game state changes
- **gamestateregistry.cpp/.h/.igamestateregistry.h**: YAML-based registry for game state flag descriptions 
- **devtools.cpp/.h**: Main mod entry points (currently incomplete)
- **game-data/**: Comprehensive YAML configuration for game state documentation
  - `global.yml`: Global game state flag descriptions
  - `maps/*.yml`: Map-specific state flag descriptions (80+ map files)
  - `schema/`: JSON schemas for validation
- **CMakeLists.txt**: Empty/minimal configuration

### Current Issues
- Code references unified mod headers: `"logger.h"`, `"okami/bitfieldmonitor.hpp"`, `"okami/memorymap.hpp"`
- Uses custom `BitFieldChangeDetector<N>` classes instead of WOLF's BitFieldMonitor system
- Missing proper WOLF mod integration (no wolf_framework.hpp usage)
- Incomplete CMake configuration
- Missing proper mod entry points and lifecycle management

### WOLF Framework Capabilities
- C API with C++ wrapper layer for cross-compiler compatibility
- BitFieldMonitor system for state change detection with callbacks
- Memory access utilities (readMemory, getModuleBase, pattern scanning)
- GUI integration through shared ImGui context
- Console command system
- Automatic resource cleanup and RAII patterns
- Event system (game tick, initialization hooks)

## Migration Requirements

1. Replace BitFieldChangeDetector with WOLF BitFieldMonitor registrations
2. Convert to proper WOLF mod structure using wolf_framework.hpp
3. Remove dependencies on unified mod infrastructure
4. Set up independent build system
5. Integrate with WOLF's logging, memory, and GUI systems
6. Maintain existing game state monitoring functionality
7. Make it an optional build target
8. Ensure independence from wolf codebase (except for src/api include path)

## Detailed Migration Plan

### Phase 1: Infrastructure Setup
**Goal**: Establish independent build system and remove unified mod dependencies

**Tasks:**
1. Configure CMakeLists.txt for standalone devtools mod
2. Set up proper WOLF framework integration and includes
3. Remove all unified mod header dependencies
4. Configure as optional build target

**Dependencies**: None
**Estimated Effort**: 1-2 hours

### Phase 2: Core Framework Migration
**Goal**: Replace unified mod lifecycle with WOLF mod pattern

**Tasks:**
1. Replace devtools.h/.cpp with proper WOLF mod class structure
2. Convert manual initialization to WOLF lifecycle hooks (earlyGameInit, lateGameInit, shutdown)
3. Integrate WOLF logging system (replace manual logger.h usage)
4. Convert GUI integration to WOLF's shared ImGui context

**Dependencies**: Phase 1 complete
**Estimated Effort**: 2-3 hours

### Phase 3: BitField Monitoring System Migration
**Goal**: Replace 80+ BitFieldChangeDetector instances with WOLF BitFieldMonitor registrations

**Tasks:**
1. Replace globalFlagsMonitor with `wolf::createBitfieldMonitor()` for GlobalGameStateFlags
2. Convert all MapState monitors (collectedObjects, commonStates, areasRestored, etc.) to WOLF registrations
3. Replace TrackerData monitors with WOLF BitFieldMonitor callbacks
4. Convert WorldStateData monitors to WOLF system
5. Remove custom BitFieldChangeDetector class dependency

**Dependencies**: Phase 2 complete
**Estimated Effort**: 4-6 hours (bulk of the work)

### Phase 4: Memory Access Modernization
**Goal**: Replace direct memory access with WOLF memory utilities

**Tasks:**
1. Convert hard-coded memory access patterns to WOLF's memory access functions
2. Replace manual pointer validation with WOLF's safe memory utilities
3. Update address resolution to use WOLF's module base + offset pattern
4. Remove dependency on unified mod's memory mapping system

**Dependencies**: Phase 3 complete
**Estimated Effort**: 2-3 hours

### Phase 5: Game State Registry Integration
**Goal**: Maintain existing YAML-based documentation system within WOLF framework

**Tasks:**
1. Update GameStateRegistry to work independently of unified mod
2. Ensure YAML loading paths work from devtools mod location
3. Update registry interface to work with WOLF logging system
4. Preserve comprehensive game state documentation functionality

**Dependencies**: Phase 2 complete (can run in parallel with Phase 3-4)
**Estimated Effort**: 1-2 hours

### Phase 6: GUI System Integration
**Goal**: Convert ImGui integration to WOLF's shared context system

**Tasks:**
1. Remove manual GUI initialization and lifecycle management
2. Update devtools GUI rendering to use WOLF's GUI integration hooks
3. Ensure proper window management and input handling
4. Maintain all existing devtools GUI functionality

**Dependencies**: Phase 2 complete
**Estimated Effort**: 2-3 hours

### Phase 7: Console Integration
**Goal**: Add console commands for devtools functionality

**Tasks:**
1. Add devtools console commands using WOLF's command system
2. Implement commands for toggling devtools window visibility
3. Add commands for common debugging operations
4. Integrate with WOLF's console help system

**Dependencies**: Phases 2, 6 complete
**Estimated Effort**: 1-2 hours

### Phase 8: Testing and Validation
**Goal**: Ensure feature parity and proper WOLF integration

**Tasks:**
1. Verify all BitField monitoring works correctly with WOLF system
2. Test GUI functionality and window management
3. Validate game state change detection matches original behavior
4. Confirm proper mod loading/unloading lifecycle
5. Test independence from main wolf codebase (except API includes)

**Dependencies**: Phases 1-7 complete
**Estimated Effort**: 2-3 hours

### Phase 9: Build System Finalization
**Goal**: Complete independent build configuration

**Tasks:**
1. Ensure devtools builds as optional target
2. Verify proper linking with WOLF API but no other wolf components
3. Configure resource embedding for YAML documentation files
4. Set up proper install targets and distribution

**Dependencies**: Phase 8 complete
**Estimated Effort**: 1-2 hours

### Phase 10: Documentation and Cleanup
**Goal**: Update documentation and remove dead code

**Tasks:**
1. Update devtools README with WOLF-based usage instructions
2. Document new console commands and integration points
3. Remove unused unified mod integration code
4. Clean up commented-out or transitional code

**Dependencies**: Phase 9 complete
**Estimated Effort**: 1 hour

## Key Technical Transformations

### BitField Monitor Migration Pattern
```cpp
// OLD (Unified Mod):
std::unique_ptr<BitFieldChangeDetector<86>> globalFlagsMonitor;
globalFlagsMonitor = std::make_unique<BitFieldChangeDetector<86>>(onGlobalFlagChange);
globalFlagsMonitor->update(*GlobalGameStateFlags.get_ptr());

// NEW (WOLF Framework):
wolf::BitfieldMonitorHandle globalMonitor = wolf::createBitfieldMonitor(
    "main.dll", GlobalGameStateFlagsOffset, 86/8, 
    onGlobalFlagChange, "Global Game State Flags");
```

### Lifecycle Integration Pattern
```cpp
// OLD: Manual initialization
void devDataFinder_OnGameTick() { 
    /* manual updates in game tick loop */ 
}

// NEW: WOLF lifecycle
class DevToolsMod {
public:
    static void lateGameInit() { 
        // Register all monitors once during initialization
        setupBitfieldMonitors();
        setupGUI();
    }
    
    static void shutdown() {
        // Automatic cleanup via RAII
    }
};
```

### Memory Access Pattern
```cpp
// OLD: Direct memory access
auto* data = GlobalGameStateFlags.get_ptr();

// NEW: WOLF memory utilities
auto moduleBase = wolf::getModuleBase("main.dll");
auto* data = wolf::readMemory<GlobalGameStateType>(moduleBase + offset);
```

## Success Criteria

- [ ] DevTools compiles as independent optional build target
- [ ] All BitField monitoring functionality preserved
- [ ] GUI integration works with WOLF's shared ImGui context
- [ ] Game state documentation system fully functional
- [ ] No dependencies on unified mod components
- [ ] Proper WOLF mod lifecycle integration
- [ ] Console commands for devtools operations
- [ ] Feature parity with existing devtools functionality

## Total Estimated Effort
**16-25 hours** across all phases, with Phase 3 (BitField migration) being the most time-intensive.

## Notes

- BitFieldMonitor replacement is the core technical challenge
- Game state registry YAML system should be preserved as-is
- Independence from wolf codebase except src/api includes is critical
- Optional build target requirement must be maintained throughout