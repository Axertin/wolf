# Wolf ImGui System

## Overview

Wolf uses a **shared ImGui context** approach where the Wolf runtime provides a single ImGui context that all mods can safely access. This design provides several key advantages:

- **Performance**: No context switching overhead between mods
- **Memory efficiency**: Single shared context reduces memory usage

## The DLL Boundary Problem

When using ImGui across DLL boundaries (which is the case with Wolf mods), there are critical memory management issues to consider:

### The Issue

ImGui allocates memory for various operations (widgets, draw lists, fonts, etc.). When this memory is allocated in one DLL (e.g., Wolf runtime) but freed in another DLL (e.g., a mod), **heap corruption occurs** because each DLL has its own memory heap on Windows.

This manifests as crashes with errors like:
- `_CrtIsValidHeapPointer` assertions
- Access violations when freeing memory
- Intermittent crashes during GUI operations

### The Solution

Wolf solves this by implementing **complete resource sharing**:

1. **Wolf runtime** sets up ImGui with specific memory allocator functions and font atlas
2. **Mods** configure their ImGui instances to use the **same allocators and context**
3. All ImGui operations use shared resources (memory, fonts, textures), preventing corruption

This approach follows ImGui's official recommendations for DLL usage as documented in `imgui.cpp` and `imgui.h`:

> "DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions() for each static/DLL boundary you are calling from."

> "Each context create its own ImFontAtlas by default. You may instance one yourself and pass it to CreateContext() to share a font atlas between contexts."

### Styled Text Limitations

Due to the complexity of sharing font atlas data across DLL boundaries, **styled text functions are not supported**:

- **Unsupported**: `ImGui::TextColored()`, `ImGui::PushStyleColor() + Text()`, and similar styled text operations
- **Supported**: All other ImGui functionality works perfectly (buttons, inputs, layout, separators, etc.)  
- **Future**: Wolf may provide wrapper functions for styled text if needed (e.g., `wolf::textColored()`)

## How It Works

### Automatic Setup

Wolf's GUI system handles allocator and context sharing automatically. When a mod calls `wolf::setImGuiContext()`, the following happens:

1. **First call only**: Wolf's allocator functions are retrieved and configured
2. **Every call**: Wolf's shared ImGui context is set as the current context
3. **Result**: The mod can safely use most ImGui functions with shared memory management

### Resource Safety

The system ensures resource safety by:

- **Shared allocators**: All ImGui allocations use Wolf's memory heap (prevents crashes)
- **Shared context**: All mods use the same ImGui context instance (prevents state conflicts)
- **Simple limitations**: Styled text functions avoided to prevent font atlas complications
- **Automatic setup**: All necessary resource sharing happens transparently

## Creating GUI Windows

### Basic Window Creation

```cpp
#include <wolf_core.hpp>
#include <wolf_gui.hpp>
#include <imgui.h>

void myGuiCallback(int outerWidth, int outerHeight, float uiScale)
{
    // CRITICAL: Set Wolf's ImGui context before any ImGui calls
    if (!wolf::setImGuiContext()) {
        wolf::logError("Failed to set ImGui context!");
        return;
    }

    // Now you can safely use most ImGui functions
    if (ImGui::Begin("My Mod Window")) {
        // SAFE - Basic text and UI elements work perfectly
        ImGui::Text("Hello from my mod!");
        ImGui::Text("Status: Connected");
        
        if (ImGui::Button("Click Me")) {
            wolf::logInfo("Button clicked!");
        }
        
        static float value = 0.0f;
        ImGui::SliderFloat("Slider", &value, 0.0f, 1.0f);
        
        static char buffer[256] = "";
        ImGui::InputText("Input", buffer, sizeof(buffer));
        
        ImGui::Separator();
        ImGui::Text("99% of ImGui functionality works great!");
        
        // AVOID - Styled text functions (will crash)
        // ImGui::TextColored(ImVec4(1,0,0,1), "Red text");  // DON'T USE
        // ImGui::PushStyleColor(ImGuiCol_Text, red); ImGui::Text("Text"); ImGui::PopStyleColor(); // DON'T USE
    }
    ImGui::End();
}

// Register the window
void lateGameInit()
{
    wolf::registerGuiWindow("My Mod Window", myGuiCallback, false);
}
```

### Window Management

```cpp
// Toggle window visibility
wolf::toggleGuiWindow("My Mod Window");

// Set specific visibility
wolf::setGuiWindowVisible("My Mod Window", true);  // Show
wolf::setGuiWindowVisible("My Mod Window", false); // Hide

// Unregister window
wolf::unregisterGuiWindow("My Mod Window");
```

### Advanced Usage

```cpp
void advancedGuiCallback(int outerWidth, int outerHeight, float uiScale)
{
    if (!wolf::setImGuiContext()) return;
    
    // Access window dimensions and UI scale
    ImGui::Text("Game window: %dx%d (scale: %.2f)", outerWidth, outerHeight, uiScale);
    
    // Use ImGui's full feature set
    if (ImGui::BeginTabBar("MyTabs")) {
        if (ImGui::BeginTabItem("Settings")) {
            // Settings content
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            // Debug content
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    
    // Custom drawing
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    draw_list->AddRectFilled(pos, ImVec2(pos.x + 50, pos.y + 50), IM_COL32(255, 0, 0, 255));
}
```

## Safe vs Unsafe ImGui Functions

### Safe Functions (Fully Supported)

99% of ImGui functionality works perfectly with Wolf's shared context:

```cpp
// Text and layout
ImGui::Text("Basic text");
ImGui::BulletText("Bullet point");
ImGui::Separator();
ImGui::Spacing();
ImGui::SameLine();

// Input controls  
ImGui::Button("Click Me");
ImGui::Checkbox("Enable Feature", &flag);
ImGui::SliderFloat("Value", &val, 0.0f, 1.0f);
ImGui::InputText("Input", buffer, size);
ImGui::InputFloat("Number", &number);

// Layout and containers
ImGui::Begin("Window");
ImGui::BeginChild("Child");
ImGui::BeginTabBar("Tabs");
ImGui::BeginTable("Table", columns);
ImGui::BeginMenuBar();

// Lists and trees
ImGui::ListBox("List", &selected, items, count);
ImGui::TreeNode("Node");
ImGui::Selectable("Item", &selected);

// And many more...
```

### Unsafe Functions (Avoid These)

Styled text functions that modify text appearance will crash:

```cpp
// DON'T USE - These will crash
ImGui::TextColored(ImVec4(1,0,0,1), "Red text");
ImGui::TextDisabled("Grayed text"); 
ImGui::PushStyleColor(ImGuiCol_Text, color);
ImGui::Text("Styled text");
ImGui::PopStyleColor();

// DON'T USE - Text style modifications
ImGui::PushStyleVar(ImGuiStyleVar_..., value); // when affecting text
```

### Future: Wolf Wrappers (If Needed)

If styled text is needed, Wolf may provide safe wrapper functions:

```cpp
// Potential future Wolf wrappers
wolf::textColored("Error message", {1, 0, 0, 1});  // Safe red text
wolf::textDisabled("Inactive option");             // Safe grayed text
wolf::beginTextStyle(WOLF_TEXT_BOLD | WOLF_TEXT_RED);
wolf::text("Bold red text");
wolf::endTextStyle();
```

## Integration with Wolf Systems

### Console Commands

You can integrate GUI windows with Wolf's console system:

```cpp
void lateGameInit()
{
    // Register GUI window
    wolf::registerGuiWindow("Debug Panel", debugGuiCallback, false);
    
    // Add console command to toggle it
    wolf::addCommand("debug_panel", [](int argc, const char** argv) {
        wolf::toggleGuiWindow("Debug Panel");
    }, "Toggle the debug panel window");
}
```

## Requirements for Mods

To use Wolf's ImGui system, your mod must:

1. **Link against ImGui**: Include ImGui in your mod's build system
2. **Include headers**: Include both `<wolf_gui.hpp>` and `<imgui.h>`
3. **Call setImGuiContext()**: Always call this before ImGui operations
4. **Register properly**: Use Wolf's registration functions, not direct ImGui calls (unless you're setting up your own backend and context)

### Example CMakeLists.txt

```cmake
# Find ImGui (assuming it's available)
find_package(imgui REQUIRED)

# Link your mod against both Wolf and ImGui
target_link_libraries(your_mod_name
    imgui::imgui
)
target_include_directories(your_mod_name PUBLIC
    ${CMAKE_SOURCE_DIR}/include/wolf_framework.hpp
)
```

## Best Practices

### Always Set Context First

```cpp
void guiCallback(int width, int height, float scale)
{
    // ALWAYS do this first
    if (!wolf::setImGuiContext()) {
        return; // Cannot continue without valid context
    }
    
    // Now ImGui calls are safe
    ImGui::Begin("Window");
    // ... GUI code ...
    ImGui::End();
}
```

### Handle Errors Gracefully

```cpp
void robustGuiCallback(int width, int height, float scale)
{
    if (!wolf::setImGuiContext()) {
        // Log the error but don't crash
        static bool errorLogged = false;
        if (!errorLogged) {
            wolf::logError("Failed to set ImGui context - GUI disabled");
            errorLogged = true;
        }
        return;
    }
    
    // Normal GUI code...
}
```

### Use Static Variables for State

Since GUI callbacks are called every frame, use static variables to maintain state:

```cpp
void settingsCallback(int width, int height, float scale)
{
    if (!wolf::setImGuiContext()) return;
    
    static bool enableFeature = false;
    static float sensitivity = 1.0f;
    static char textBuffer[256] = "";
    
    if (ImGui::Begin("Settings")) {
        ImGui::Checkbox("Enable Feature", &enableFeature);
        ImGui::SliderFloat("Sensitivity", &sensitivity, 0.0f, 2.0f);
        ImGui::InputText("Text Input", textBuffer, sizeof(textBuffer));
    }
    ImGui::End();
}
```

## Troubleshooting

### Common Issues

1. **Heap corruption crashes**
   - **Cause**: Not calling `wolf::setImGuiContext()` before ImGui operations
   - **Solution**: Always call `wolf::setImGuiContext()` first in GUI callbacks

2. **ImGui functions don't work**
   - **Cause**: No valid ImGui context set
   - **Solution**: Ensure `wolf::setImGuiContext()` returns `true`

3. **Styled text crashes (TextColored, PushStyleColor + Text)**
   - **Cause**: Cross-DLL font atlas complications
   - **Solution**: Use basic `ImGui::Text()` instead, avoid styled text functions

4. **Windows not responding to input**
   - **Cause**: GUI callback not being called  
   - **Solution**: Ensure window is registered and set to visible

### Debugging Tips

```cpp
void debugGuiCallback(int width, int height, float scale)
{
    if (!wolf::setImGuiContext()) {
        wolf::logError("Context setup failed!");
        return;
    }
    
    if (ImGui::Begin("Debug Info")) {
        // Show allocator information
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void* userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        
        ImGui::Text("Allocator: %p", (void*)allocFunc);
        ImGui::Text("Free func: %p", (void*)freeFunc);
        ImGui::Text("User data: %p", userData);
        
        // Show context information
        ImGui::Text("Context: %p", ImGui::GetCurrentContext());
        ImGui::Text("Frame count: %d", ImGui::GetFrameCount());
    }
    ImGui::End();
}
```

## Technical Details

### Memory Allocator Functions

Wolf exposes three functions for allocator sharing:

- `getImGuiAllocFunc()`: Returns Wolf's memory allocation function
- `getImGuiFreeFunc()`: Returns Wolf's memory free function  
- `getImGuiAllocUserData()`: Returns user data for allocator functions

These are automatically used by `wolf::setImGuiContext()` to configure allocator sharing.

### Performance Considerations

- **Shared context**: No performance overhead from context switching
- **Memory efficiency**: Single context reduces memory usage vs. independent contexts
- **Render batching**: All GUI elements can be batched together for efficient rendering

### Platform Compatibility

This system works on:
- **Windows**: Direct execution
- **Linux**: Through Proton/Wine

The allocator sharing is particularly critical on Windows due to stricter heap management, but provides consistency across all platforms.

## Migration from Other Systems

### From Independent ImGui Contexts

If you previously used independent ImGui contexts:

```cpp
// OLD approach (independent context)
ImGuiContext* myContext = ImGui::CreateContext();
ImGui::SetCurrentContext(myContext);
// ... ImGui calls ...
ImGui::DestroyContext(myContext);

// NEW approach (shared context)
void guiCallback(int width, int height, float scale) {
    if (!wolf::setImGuiContext()) return;
    // ... same ImGui calls ...
}
wolf::registerGuiWindow("Window", guiCallback);
```

### From Raw ImGui Integration

If you previously integrated ImGui directly:

```cpp
// OLD approach (manual integration)
ImGui_ImplWin32_Init(hwnd);
ImGui_ImplDX11_Init(device, context);
// ... render loop ...
ImGui_ImplDX11_Shutdown();
ImGui_ImplWin32_Shutdown();

// NEW approach (Wolf integration)  
// Wolf handles all backend setup
wolf::registerGuiWindow("Window", guiCallback);
```

## Future Enhancements

Potential future improvements to Wolf's ImGui system:

- **Font management**: System for mods to register custom fonts
- **Input filtering**: More granular control over input capture
