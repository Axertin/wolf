# WOLF Framework Examples

This directory contains example mods/plugins that demonstrate how to use the WOLF API.

## Hello World Example

A simple example mod that demonstrates:

- Basic mod initialization (early and late game init)
- Logging with custom prefixes
- Console command registration
- Game event callbacks (game start, item pickup)
- Proper mod lifecycle management

### Building

```bash
cmake -B build && cmake --build build
```

The mod builds cleanly as a standalone DLL with no linking dependencies on the runtime.

### Features Demonstrated

1. **Mod Registration**: Uses `WOLF_MOD_ENTRY_CLASS` macro for clean mod registration
2. **Logging**: Shows how to use `wolf::logInfo()` and `wolf::setLogPrefix()`
3. **Console Commands**: Registers a "hello" command that can take optional arguments
4. **Event Callbacks**: Demonstrates `wolf::onGameStart()` and `wolf::onItemPickup()`
5. **Console Output**: Shows how to print to the game console

### Usage

Once loaded, the mod provides:
- A "hello" console command (try "hello" or "hello YourName")
- Automatic logging when game starts
- Item pickup notifications in the log

This example showcases the basic patterns for creating WOLF framework mods.