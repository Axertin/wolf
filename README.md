# WOLF - The WOLF Okami Loader Framework

A modding framework for Okami HD (Steam) that provides a robust, cross-compiler compatible foundation for game modifications.

## Overview

WOLF uses DLL injection to load into the game process and provides a modern C++ API for mod developers while maintaining a stable C runtime interface.

### Components

- **Loader** (`wolf-loader.exe`) - Launches game and bootstraps the runtime
- **Runtime** (`dinput8.dll`) - Core framework services injected into game process  
- **Framework** (`wolf_framework.hpp`) - Single-header C++17+ library for mod development

## Quick Start

### Installing / Using

Extract the WOLF zip into your game folder (so that `dinput8.dll` and `wolf-loader.exe` are in the same directory as `okami.exe`).

#### On Windows

Launch `wolf-loader.exe`

#### On Linux

In `Okami HD > Properties > General`, set Launch Options to:

```bash
WINEDLLOVERRIDES="dinput8=n,b" %command% -MODDED
```

Then launch the game through Steam.

### Building

Uses CMake with vcpkg for dependencies.

```bash
# Configure
cmake --preset x64-clang-debug

# Build
cmake --build --preset x64-clang-debug
```

### Creating a Mod

```cpp
#include "wolf_framework.hpp"

class MyMod {
public:
    static void earlyGameInit() {
        // Hook setup before game initialization
    }
    
    static void lateGameInit() {
        // Normal mod initialization
    }
    
    static void shutdown() {
        // Cleanup
    }
    
    static const char* getName() { return "MyMod"; }
    static const char* getVersion() { return "1.0.0"; }
};

WOLF_MOD_ENTRY_CLASS(MyMod)
```

Place compiled mod DLLs in `mods/` directory.

## Features

- Memory access and pattern scanning
- Game event callbacks and custom hooks
- Console command system
- ImGui integration for mod UIs
- Resource file interception
- Bitfield monitoring for game state
- Cross-compiler compatibility (MSVC, Clang, GCC)

## Status

Early development - API subject to change. Documentation contained in docs/
