#pragma once

#include <string>

/**
 * @brief Base class for all ImGui windows
 */
class Window
{
  public:
    Window(const std::string &windowName) : name(windowName), IsVisible(false)
    {
    }
    virtual ~Window() = default;

    /**
     * @brief Toggle window visibility
     */
    virtual void toggleVisibility()
    {
        IsVisible = !IsVisible;
    }

    /**
     * @brief Draw the window
     * @param OuterWidth Window width
     * @param OuterHeight Window height
     * @param UIScale UI scaling factor
     */
    virtual void draw(int OuterWidth, int OuterHeight, float UIScale) = 0;

    std::string name;
    bool IsVisible;
};