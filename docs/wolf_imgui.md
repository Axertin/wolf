# Cross-DLL ImGui Implementation

The Wolf modding framework implements a complex cross-DLL ImGui setup that allows mods to create GUI windows while sharing resources with the main Wolf runtime. This document explains the architecture, implementation details, and the various workarounds needed to make ImGui work across DLL boundaries.

## Architecture Overview

### The Big Modloader Problem

ImGui was designed primarily for single-executable applications. When used across DLL boundaries with static linking, several issues arise:

1. **Global Context Isolation**: Each DLL has its own `GImGui` global context pointer
2. **Static Data Isolation**: Font atlases, allocators, and other static data remain module-specific
3. **Backend Conflicts**: Multiple Win32 backends can't coexist properly
4. **Input Event Timing**: Character events get lost due to context switching timing

### The Duct Tape Solution

- **Wolf Runtime**: Owns the primary ImGui context with Win32 + D3D11 backends
- **Mod DLLs**: Each has its own ImGui context sharing Wolf's font atlas and D3D11 device
- **Centralized Rendering**: Wolf collects all mod draw data and renders it together
- **Input Forwarding**: Wolf captures all input and forwards it to mod contexts

## Implementation Details

### Context Management

#### Wolf Runtime Context
- **Creation**: `ImGui::CreateContext()` with default font atlas
- **Backends**: `ImGui_ImplWin32_Init()` + `ImGui_ImplDX11_Init()`
- **Lifecycle**: Lives for basically the entire application lifetime
- **Responsibilities**: Input capture, font management, primary rendering

#### Mod Contexts
- **Creation**: `ImGui::CreateContext(shared_font_atlas)` with Wolf's atlas
- **Backends**: D3D11 only via `WOLF_IMGUI_INIT_BACKEND()` macro
- **Lifecycle**: Created in `lateGameInit()`, destroyed in `shutdown()`
- **Responsibilities**: Mod-specific GUI rendering only

### Rendering Pipeline

The rendering happens in this specific order each frame:

1. **Wolf Pre-Processing**
   ```cpp
   // Enable keyboard capture if any mod needs it
   if (hasModContexts()) {
       ImGui::SetNextFrameWantCaptureKeyboard(true);
   }
   
   ImGui_ImplWin32_NewFrame();  // Process Win32 input
   ImGui_ImplDX11_NewFrame();
   ImGui::NewFrame();
   ```

2. **Wolf GUI Rendering**
   ```cpp
   // Wolf renders its own GUI (console, debug windows, etc.)
   for (auto& window : Windows) {
       window->draw(width, height, uiScale);
   }
   ImGui::Render();
   ```

3. **Input Forwarding**
   ```cpp
   // Forward Wolf's input state to all mod contexts
   wolf::runtime::internal::forwardInputToModContexts();
   ```

4. **Mod GUI Rendering**
   ```cpp
   // Render mod GUI windows and collect draw data
   wolf::runtime::internal::renderModGuiWindows(swapChain);
   ```

5. **Final Rendering**
   ```cpp
   // Set render target and render Wolf's GUI
   context->OMSetRenderTargets(1, &rtv, nullptr);
   ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
   
   // Render all collected mod draw data
   wolf::runtime::internal::renderCollectedModDrawData();
   ```

### Input Event Handling

#### Mouse and Keyboard State
Standard ImGui input state is copied from Wolf's context to mod contexts. Not everything is, but enough to allow mods to handle most things sanely.

#### Mouse Click Events
Mouse click events are forwarded to enable widget focus. 

#### Character Input (Special Handling)
Due to cross-DLL timing issues / things I don't quite understand yet, character input requires special handling:

Wolf's `InputQueueCharacters` is empty when `forwardInputToModContexts()` is called. This is because ImGui's Win32 backend doesn't properly handle WM_CHAR messages in cross-DLL scenarios like ours (we're kinda on the very edge of what ImGui supports at all). SO, we directly forward characters to all the mod contexts from WndProc.

This bypasses Wolf's input queue entirely and immediately forwards characters to mod contexts during message processing.

## Mod Integration

### Required Macros

Mods use a set of macros to integrate with Wolf's ImGui system:

```cpp
// Setup shared allocators and font atlas (called once in lateGameInit)
if (!wolf::setupSharedImGuiAllocators()) {
    // Handle error
}

// Initialize D3D11 backend for this mod's context
WOLF_IMGUI_INIT_BACKEND();

// Per-frame rendering (called in registered GUI callback)
WOLF_IMGUI_BEGIN(outerWidth, outerHeight, uiScale);

if (ImGui::Begin("My Window")) {
    ImGui::Text("Hello from mod!");
}
ImGui::End();

WOLF_IMGUI_END();
```

### Macro Implementation

#### `WOLF_IMGUI_BEGIN`
- Sets mod's ImGui context as current
- Handles font atlas recovery if invalid
- Calls `ImGui::NewFrame()` for mod context

#### `WOLF_IMGUI_END`  
- Calls `ImGui::Render()` for mod context
- Registers draw data with Wolf for later rendering
- Restores Wolf's ImGui context

#### `WOLF_IMGUI_INIT_BACKEND`
- Initializes D3D11 backend with Wolf's shared device
- Sets up proper render state for mod context

## Troubleshooting

### Debug Strategies

1. **Context Verification**: Ensure mod contexts are properly registered
2. **Font Atlas State**: Check `ImGui::GetIO().Fonts->IsBuilt()` status
3. **Input State**: Verify `WantCaptureKeyboard/Mouse` flags are set correctly
4. **Draw Data**: Confirm mod draw data is being collected and rendered

## Performance Considerations

- **Context Switching**: Frequent `SetCurrentContext()` calls have overhead. Unfortunately unavoidable afaik with this sort of cross-dll tomfoolery.
- **Memory Usage**: Each mod context maintains its own vertex/index buffers. Again, unavoidable afaik.
- **Draw Calls**: All mod draw data is batched and rendered together to minimize state changes, in an attempt to claw back a bit of performance.

ImGUI overhead still shouldn't be that much of a worry, the game draws at 30fps anyway so we have plenty of time on the render thread to do all this switching.

## Future Improvements

- **Mod UI Flicker**: When the font atlas is reloaded (like when certain text-based things happen like the console being rendered for the first time), all mod windows have an invalid atlas for a few frames. During this time, mod window rendering does not happen, causing a flicker.
- **Thread Safety**: Currently not thread-safe due to global context switching (probably realistically insurmountable)
- **Memory Optimization**: Could implement shared vertex/index buffer pools
