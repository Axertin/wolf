#pragma once

#include <imgui.h>

#include "window.h"

/**
 * @brief Developer tools window (placeholder)
 */
class DevTools : public Window
{
  public:
    DevTools() : Window("DevTools")
    {
    }

    void draw([[maybe_unused]] int OuterWidth, [[maybe_unused]] int OuterHeight, [[maybe_unused]] float UIScale) override
    {
        if (!IsVisible)
            return;

        if (ImGui::Begin(name.c_str(), &IsVisible))
        {
            ImGui::Text("Developer Tools");
            ImGui::Separator();
            ImGui::Text("Placeholder - memory debugger, function hooks, etc. coming soon...");
        }
        ImGui::End();
    }
};