#include <filesystem>

#include "devdatafinder.h"
#include "devtools_gui.h"
#include "gamestateregistry.h"
#include "imgui.h"
#include "wolf_framework.hpp"

/**
 * @brief DevTools WOLF Mod - Game investigation and trainer tools
 */
class DevToolsMod
{
  public:
    static void earlyGameInit()
    {
        // Initialize game state registry
        GameStateRegistry::initialize(getGameDataPath());
    }

    static void lateGameInit()
    {
        // Initialize GUI
        initializeDevToolsGUI();

        // Initialize data finder monitoring
        initializeDevDataFinder();
    }

    static void shutdown()
    {
    }

    static const char *getName()
    {
        return "DevTools";
    }

    static const char *getVersion()
    {
        return "1.0.1";
    }

  private:
    static std::filesystem::path getGameDataPath()
    {
        // Get the devtools mod directory and append game-data
        // This assumes the YAML files are installed alongside the mod
        auto modDir = std::filesystem::path("mods/devtools");
        return modDir / "game-data";
    }
};

// Register the mod with WOLF
WOLF_MOD_ENTRY_CLASS(DevToolsMod)
