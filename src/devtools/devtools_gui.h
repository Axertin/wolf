#pragma once

/**
 * @brief Initialize DevTools GUI system
 *
 * Sets up ImGui integration and registers GUI callback with WOLF framework.
 * Call this once during mod initialization.
 */
void initializeDevToolsGUI();

/**
 * @brief Render DevTools debug window
 *
 * Called automatically by WOLF's GUI system each frame.
 * Shows game state information and debugging tools.
 *
 * @param width Outer window width
 * @param height Outer window height
 * @param scale UI scale factor
 */
void renderDevToolsWindow(int width, int height, float scale);