#include "devdatafinder.h"
#include "wolf_framework.hpp"
#include "gamestateregistry.h"

#include <okami/structs.hpp>
#include <okami/maptype.hpp>

// Memory offsets from the unified mod (main.dll base + offset)
constexpr uintptr_t GLOBAL_GAME_STATE_FLAGS_OFFSET = 0xB6B2AC;
constexpr uintptr_t MAP_DATA_OFFSET = 0xB322B0;
constexpr uintptr_t AMMY_TRACKER_OFFSET = 0xB21780;
constexpr uintptr_t AMMY_COLLECTIONS_OFFSET = 0xB205D0;

// WOLF BitField Monitor handles
static wolf::BitfieldMonitorHandle globalFlagsMonitor;
static std::vector<wolf::BitfieldMonitorHandle> mapStateMonitors;

// Helper function to log BitField changes with documentation
static void logBitFieldChange(const char* fieldName, unsigned int bitIndex, 
                             bool oldValue, bool newValue, 
                             const std::unordered_map<unsigned, std::string>& documentation)
{
    if (!documentation.contains(bitIndex))
    {
        wolf::logWarning("[Undocumented] BitField %s index %u was changed from %d to %d", 
                        fieldName, bitIndex, oldValue, newValue);
    }
    else
    {
        wolf::logInfo("BitField %s index %u (%s) was changed from %d to %d", 
                     fieldName, bitIndex, documentation.at(bitIndex).c_str(), 
                     oldValue, newValue);
    }
}

// Callback for global game state flag changes
static void onGlobalFlagChange(unsigned int bitIndex, bool oldValue, bool newValue)
{
    const auto& registry = GameStateRegistry::instance();
    const auto& globalDoc = registry.getGlobalConfig().globalGameState;
    
    logBitFieldChange("GlobalGameState", bitIndex, oldValue, newValue, globalDoc);
}

// Callback generators for map state changes
static std::function<void(unsigned int, bool, bool)> createMapStateCallback(int mapId)
{
    return [mapId](unsigned int bitIndex, bool oldValue, bool newValue) {
        const auto& registry = GameStateRegistry::instance();
        const auto& mapConfig = registry.getMapConfig(static_cast<okami::MapTypes::Enum>(mapId));
        
        std::string mapName = okami::MapTypes::GetName(static_cast<unsigned>(mapId));
        std::string fieldName = "(" + mapName + ") MapState::worldStateBits";
        
        logBitFieldChange(fieldName.c_str(), bitIndex, oldValue, newValue, mapConfig.worldStateBits);
    };
}

void initializeDevDataFinder()
{
    wolf::logInfo("DevTools: Initializing data finder system");
    
    auto moduleBase = wolf::getModuleBase("main.dll");
    if (moduleBase == 0)
    {
        wolf::logError("DevTools: Failed to get main.dll module base");
        return;
    }
    
    // Set up global game state monitoring
    globalFlagsMonitor = wolf::createBitfieldMonitor(
        moduleBase + GLOBAL_GAME_STATE_FLAGS_OFFSET,
        (86 + 7) / 8,  // 86 bits converted to bytes
        onGlobalFlagChange,
        "Global Game State Flags"
    );
    
    if (!globalFlagsMonitor)
    {
        wolf::logError("DevTools: Failed to create global flags monitor");
        return;
    }
    
    // Set up map state monitoring (for now just worldStateBits of each map)
    mapStateMonitors.reserve(okami::MapTypes::NUM_MAP_TYPES);
    
    for (int mapId = 0; mapId < okami::MapTypes::NUM_MAP_TYPES; mapId++)
    {
        // Calculate offset for this map's worldStateBits
        // MapData is an array of MapState structs, worldStateBits is at offset 0x0 in MapState
        uintptr_t mapStateBitsOffset = MAP_DATA_OFFSET + (mapId * sizeof(okami::MapState));
        
        auto monitor = wolf::createBitfieldMonitor(
            moduleBase + mapStateBitsOffset,
            (256 + 7) / 8,  // 256 bits converted to bytes
            createMapStateCallback(mapId),
            ("Map " + std::to_string(mapId) + " WorldStateBits").c_str()
        );
        
        if (!monitor)
        {
            wolf::logWarning("DevTools: Failed to create monitor for map %d", mapId);
        }
        
        mapStateMonitors.push_back(monitor);
    }
    
    // Register cleanup handler
    wolf::registerCleanupHandler([]() {
        shutdownDevDataFinder();
    });
    
    wolf::logInfo("DevTools: Data finder initialization complete (%d monitors active)", 
                 1 + static_cast<int>(mapStateMonitors.size()));
}

void shutdownDevDataFinder()
{
    wolf::logInfo("DevTools: Shutting down data finder system");
    
    // WOLF framework handles cleanup automatically when monitors go out of scope
    // or the mod shuts down, so we don't need to manually destroy them
    
    wolf::logInfo("DevTools: Data finder shutdown complete");
}