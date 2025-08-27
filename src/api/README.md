# Wolf API Organization

This directory contains the modular Wolf Framework API headers, organized by functionality for easier maintenance.

The build system combines these headers in into a single flattened header, for plugins and mods to use.

## Structure

The Wolf API is split into the following headers:

- **`wolf_core.hpp`** - Core framework, mod registration, and C API definitions
- **`wolf_logging.hpp`** - Logging system (logInfo, logWarning, etc.)
- **`wolf_memory.hpp`** - Memory access system (MemoryAccessor, memory watching)
- **`wolf_bitfield.hpp`** - Bitfield monitoring system for game state tracking
- **`wolf_hooks.hpp`** - Game hooks and callbacks (onGameTick, hookFunction, etc.)
- **`wolf_console.hpp`** - Console command system
- **`wolf_resources.hpp`** - Resource interception system
- **`wolf_gui.hpp`** - ImGui integration and custom windows

## Maintenance

When modifying the API:

1. **Edit the appropriate header** in `src/api/`
2. **Dependencies** work like normal C++ headers:
   - Add `#include "wolf_core.hpp"` etc. for IntelliSense
   - System automatically determines correct order from includes
   - All includes are stripped in flattened output
   - Priority list in `flatten_api.cmake` handles edge cases

3. **Flattening happens automatically** during build, or run manually:
   ```bash
   cmake --build . --target flatten_api
   ```
4. The system automatically:
   - Discovers any `wolf_*.hpp` files (like `generate_sources.cmake` does for `.cpp`)
   - Includes them in both development and distribution builds  
   - Analyzes `#include` dependencies to determine correct order
   - Strips all includes during flattening
