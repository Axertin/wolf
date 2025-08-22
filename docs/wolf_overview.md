# WOLF Modding Framework Architecture Overview

## Executive Summary

The WOLF Okami Loader Framework is a comprehensive modding framework designed for the Steam release of Okami HD, implementing a three-component architecture that provides robust, cross-compiler-compatible mod support through a well-defined abstraction layer. The framework combines DLL injection techniques with a modern C++ interface while maintaining compatibility across different toolchains and languages through a stable C API foundation.

## Architecture Overview

### Core Design Philosophy
 
WOLF follows several key architectural principles:

1. **Separation of Concerns**: Clean separation between injection, runtime services, and mod interface layers
2. **Compiler Independence**: Runtime uses a plain C API to ensure compatibility across different compilers and potentially other languages
3. **Modern C++ Interface**: Framework layer provides modern C++ conveniences while maintaining underlying C compatibility
4. **Transparent Integration**: Mods integrate into the game's execution flow through strategic hooking and other means, with emphasis placed on minimizing overhead while providing design convenience

### System Components

The WOLF framework consists of three primary components working in concert:

#### 1. WOLF Loader (`src/loader/` → `wolf-loader.exe`)
- **Purpose**: Game launcher and initial setup
- **Architecture**: Standalone executable that manages the initial bootstrap process
- **Responsibilities**:
  - Launches the target game executable
  - Copies the runtime DLL to the game directory
  - Creates temporary signaling files for runtime detection
  - Discovers and reports available mods

#### 2. WOLF Runtime (`src/runtime/` → `dinput8.dll`)
- **Purpose**: Core framework services and game integration
- **Architecture**: DLL proxy that masquerades as `dinput8.dll`
- **Responsibilities**:
  - **Process Injection**: Loads into the game process via DLL proxy technique
  - **Mod Loading**: Discovers and loads mod DLLs from the `mods/` directory
  - **API Implementation**: Provides all runtime services through a C API function table
  - **Game Integration**: Hooks critical game functions to inject framework functionality
  - **Resource Management**: Manages mod lifecycles, memory watching, and cleanup

#### 3. WOLF Framework (`src/api/` → `build/*/dist/wolf_framework.hpp`)
- **Purpose**: High-level C++ interface for mod developers
- **Architecture**: Single-header library generated from modular source files
- **Responsibilities**: Wrapper around the runtime's C API, providing:
  - Type-safe C++ interfaces
  - RAII resource management
  - Modern C++ conveniences (lambdas, templates, etc.)
  - Automatic cleanup handling

## Component Interaction Flow

```
Game Process Startup
        ↓
1. Game loads dinput8.dll (our runtime proxy)
        ↓
2. Runtime initializes, hooks game initialization
        ↓
3. Game calls hooked initialization function (flower_startup)
        ↓
4. Runtime discovers and loads mod DLLs
        ↓
5. Each mod calls wolfGetModInterface() with runtime API
        ↓
6. Runtime calls early init for all mods (for operations which must preceede the game's own init)
        ↓
7. Game performs initialization
        ↓
8. Runtime calls late init for all mods (for normal, non-time-critical initialization)
        ↓
9. Game runs with mod functionality active
        ↓
10. On shutdown, runtime calls mod cleanup functions
```

## Injection and Loading Mechanism

### DLL Proxy Injection

WOLF uses a DLL proxy approach targeting `dinput8.dll`:

1. **System DLL Replacement**: The runtime DLL is placed in the game directory as `dinput8.dll`
2. **Early Hooks**: During DLL initialization, runtime hooks `CreateWindowExW` to detect when the game loads `main.dll`
3. **Game Integration**: When `main.dll` is available, the runtime hooks game-specific functions like `flower_startup`

This approach provides several advantages:
- **Compatibility**: Proton support is much easier
- **Transparency**: Game operates normally with runtime services injected
- **Timing Control**: Allows precise control over when mod initialization occurs

### Mod Discovery and Loading

The runtime implements a straightforward mod loading system:

1. **Discovery**: Recursively scans the `mods/` directory for `.dll` files
2. **Loading**: Uses `LoadLibraryW()` to load each discovered mod DLL
3. **Interface Detection**: Looks for the `wolfGetModInterface` export in each DLL
4. **Registration**: Creates runtime API function table and passes it to the mod
5. **Version Validation**: Checks framework version compatibility before initialization
6. **Lifecycle Management**: Tracks loaded mods for proper shutdown handling

## API Design and Compatibility

### Two-Layer API Architecture

WOLF implements a two-layer API design to balance compatibility with usability:

#### C Runtime API (Lower Layer)
- **Location**: `src/runtime/wolf_runtime_api.h`
- **Purpose**: Stable, cross-compiler compatible interface
- **Characteristics**:
  - Plain C functions with explicit calling conventions (`__cdecl`)
  - No C++ name mangling or ABI dependencies
  - Fixed data structures and opaque handles
  - Maximum compatibility across compilers

#### C++ Framework API (Upper Layer)
- **Location**: `src/api/wolf_*.hpp` → `build/*/dist/wolf_framework.hpp`
- **Purpose**: Modern, type-safe interface for mod developers
- **Characteristics**:
  - Template-based type safety
  - RAII resource management
  - Lambda support for callbacks
  - Automatic cleanup registration

### API Function Table Injection

The runtime provides services through a function table injection pattern:

```cpp
typedef struct WolfRuntimeAPI {
    // Mod lifecycle
    WolfModId(__cdecl *getCurrentModId)(void);
    WolfModId(__cdecl *registerMod)(const WolfModInterface *modInterface);
    
    // Memory access
    uintptr_t(__cdecl *getModuleBase)(const char *module_name);
    int(__cdecl *readMemory)(uintptr_t address, void *buffer, size_t size);
    
    // Game hooks & callbacks
    void(__cdecl *registerGameTick)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    // ... additional functions
} WolfRuntimeAPI;
```

This approach provides:
- **Version Independence**: Function table can be extended without breaking existing mods
- **Runtime Flexibility**: Implementation details can change without affecting mod code
- **Cross-Language Support**: C function pointers work from any language with C interop, and any compliant C/C++ compiler

## Core Framework Services

### Memory Management
- **Module Base Resolution**: Locates loaded game modules for address calculations
- **Safe Memory Access**: Provides validated read/write operations
- **Pattern Scanning**: Finds code patterns for function location
- **Memory Watching**: Monitors memory regions for changes with callback notifications

### Game Event System
- **Lifecycle Hooks**: Early/late initialization, shutdown callbacks
- **Game State Events**: Game start/stop, menu transitions
- **Gameplay Events**: Item pickup, state changes
- **Custom Hooks**: Direct function hooking with MinHook integration

### Console System
- **Command Registration**: Dynamic command system with help text
- **Execution Engine**: Parses and executes console commands
- **Output Management**: Centralized console output handling

### GUI Integration
- **Shared ImGui Context**: Wolf provides a shared ImGui context with automatic allocator sharing to prevent heap corruption across DLL boundaries
- **Window Management**: Registration and lifecycle of custom mod windows
- **Memory Safety**: Automatic setup of shared memory allocators ensures safe ImGui usage across mods
- **Event Handling**: Proper integration with game's render loop and input capture management
- **Cross-Platform**: Supports D3D11 + Win32 backend for Windows and Proton compatibility

*See [Wolf ImGui System](wolf_imgui.md) for detailed GUI information*

### Resource Interception
- **File Replacement**: Redirect game asset loading to mod-provided files
- **Pattern Matching**: Support for wildcard-based resource interception
- **Dynamic Routing**: Runtime decision-making for resource providers

### Bitfield Monitoring
- **State Tracking**: Monitor game state stored in bitfield structures
- **Change Detection**: Callbacks when specific bits change value
- **Flexible Addressing**: Support for absolute addresses or module+offset

## Mod Development Interface

### Entry Point Pattern

Mods use a standardized entry point pattern:

```cpp
// Option 1: Direct function registration
WOLF_MOD_ENTRY(earlyInit, lateInit, shutdown, getName, getVersion)

// Option 2: Class-based approach
class MyMod {
public:
    static void earlyGameInit() { /* setup hooks */ }
    static void lateGameInit() { /* UI integration */ }
    static void shutdown() { /* cleanup */ }
    static const char* getName() { return "MyMod"; }
    static const char* getVersion() { return "1.0.0"; }
};
WOLF_MOD_ENTRY_CLASS(MyMod)
```

### Automatic Resource Management

The framework provides automatic cleanup through RAII and registration:

```cpp
// Resources are automatically cleaned up during mod shutdown
wolf::registerCleanupHandler([]() {
    // Custom cleanup logic
});

// Memory watches, GUI windows, console commands are automatically unregistered
```

### Type-Safe Wrapper System

The C++ framework layer provides type-safe wrappers around the C API:

```cpp
// Framework layer (C++)
inline void onItemPickup(std::function<void(int, int)> callback) {
    // Automatic storage management and type conversion
    auto stored = detail::addItemPickupCallback(std::move(callback));
    detail::g_runtime->registerItemPickup(
        detail::getCurrentModId(), 
        detail::itemPickupCallbackWrapper, 
        stored
    );
}

// Runtime layer (C)
void wolfRuntimeRegisterItemPickup(WolfModId mod_id, 
                                  WolfItemPickupCallback callback, 
                                  void *userdata);
```

This should assist with intellisense resolution and self-documentation.

## Build System Integration

### Cross-Compiler Support

Wolf intends to support all of the "Big 3" compilers, for both itself and mods:
- **MSVC**: Micro$oft
- **Clang**: Releases typically compiled with Clang in C++23 mode
- **GCC/MinGW**: For cross-compilation to NT and *nix development

### API Header Flattening

A Python script manages the single-header generation, which expands wolf includes in `src/api/wolf_framework.hpp` to generate a flat, single-file API which can be included in mod sources.

## Version Compatibility

### Semantic Versioning

WOLF uses semantic versioning with compatibility checking:

```cpp
#define WOLF_VERSION_INT ((MAJOR << 32) | (MINOR << 16) | PATCH)
```

### ABI Stability

The framework intends to maintain ABI stability through:
- **Function Table Extension**: New functions can be added without breaking existing mods
- **Opaque Handles**: Implementation details hidden behind opaque pointers
- **Explicit Calling Conventions**: C calling convention prevents mangling issues
- **Version Validation**: Runtime checks ensure compatible framework versions

This architecture attempts to provide a robust, extensible foundation for game modification while maintaining compatibility, performance, and ease of use for mod developers.
