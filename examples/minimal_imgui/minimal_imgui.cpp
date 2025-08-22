#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <wolf_framework.hpp>

class MinimalImGuiMod
{
  public:
    static void earlyGameInit()
    {
    }

    static void lateGameInit()
    {
        // Setup mod-specific ImGui context with Wolf's shared resources
        if (!wolf::setupSharedImGuiAllocators())
        {
            wolf::logError("Failed to setup mod ImGui context!");
            return;
        }

        // Initialize D3D11 backend for this mod's context
        WOLF_IMGUI_INIT_BACKEND();

        wolf::logInfo("About to register window");

        // Register a simple test window
        wolf::registerGuiWindow(
            "ImGui Test",
            [](int outerWidth, int outerHeight, float uiScale)
            {
                WOLF_IMGUI_BEGIN(outerWidth, outerHeight, uiScale);

                if (ImGui::Begin("Minimal Test Window"))
                {
                    ImGui::Text("Hello from ImGui!");
                    ImGui::Text("Window size: %dx%d", outerWidth, outerHeight);
                    ImGui::Text("UI Scale: %.2f", uiScale);
                }
                ImGui::End();

                WOLF_IMGUI_END();
            },
            true); // Initially visible

        wolf::logInfo("GUI window registered");
    }

    static void shutdown()
    {
        wolf::cleanupImGuiContext();
    }

    static const char *getName()
    {
        return "Minimal ImGui Test";
    }

    static const char *getVersion()
    {
        return "1.0.0";
    }
};

WOLF_MOD_ENTRY_CLASS(MinimalImGuiMod)
