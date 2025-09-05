#include <filesystem>

#include "devdatafinder.h"
#include "devtools_gui.h"
#include "gamestateregistry.h"
#include "wolf_framework.hpp"

/**
 * @brief DevTools WOLF Mod - Game investigation and trainer tools
 */
class DevToolsMod
{
  public:
    static void earlyGameInit()
    {
        wolf::logInfo("DevTools: Early game initialization");

        // Initialize game state registry
        GameStateRegistry::initialize(getGameDataPath());
    }

    static void lateGameInit()
    {
        wolf::logInfo("DevTools: Late game initialization");

        // Initialize GUI
        initializeDevToolsGUI();

        // Initialize data finder monitoring
        initializeDevDataFinder();

        wolf::logInfo("DevTools: Initialization complete");
    }

    static void shutdown()
    {
        wolf::logInfo("DevTools: Shutting down");

        // Cleanup happens automatically via WOLF's RAII system
        // and registerCleanupHandler calls
    }

    static const char *getName()
    {
        return "DevTools";
    }

    static const char *getVersion()
    {
        return "1.0.0";
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