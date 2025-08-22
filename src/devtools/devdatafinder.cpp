#include "devdatafinder.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <okami/maptype.hpp>
#include <okami/structs.hpp>

#include "gamestateregistry.h"
#include "memory_accessors.h"
#include "wolf_framework.hpp"

namespace
{
bool initialized = false;

// WOLF BitField Monitor handles for comprehensive monitoring
wolf::BitfieldMonitorHandle globalFlagsMonitor;

// MapState monitors - one for each BitField in MapState for each map
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> collectedObjectsMonitors;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> commonStatesMonitors;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> areasRestoredMonitors;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> treesBloomedMonitors;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> cursedTreesBloomedMonitors;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> fightsCleared;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> npcHasMoreToSay;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> npcUnknown;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> mapsExplored;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> field_DC;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES> field_E0;

// TrackerData monitors
wolf::BitfieldMonitorHandle trackerGameProgressionMonitor;
wolf::BitfieldMonitorHandle trackerAnimalsFedFirstTimeMonitor;
wolf::BitfieldMonitorHandle trackerField34Monitor;
wolf::BitfieldMonitorHandle trackerField38Monitor;
wolf::BitfieldMonitorHandle trackerBrushUpgradesMonitor;
wolf::BitfieldMonitorHandle trackerOptionFlagsMonitor;
wolf::BitfieldMonitorHandle trackerAreasRestoredMonitor;

// CollectionData WorldStateData monitors
wolf::BitfieldMonitorHandle worldKeyItemsAcquiredMonitor;
wolf::BitfieldMonitorHandle worldGoldDustsAcquiredMonitor;
std::array<wolf::BitfieldMonitorHandle, okami::MapTypes::NUM_MAP_TYPES + 1> worldMapStateBitsMonitors;
wolf::BitfieldMonitorHandle worldAnimalsFedBitsMonitor;

// Helper functions
void warn(const char *format, auto... args)
{
    std::string newFmt = std::string("[Undocumented] ") + format;
    wolf::logWarning(newFmt.c_str(), args...);
}

void onGlobalFlagChange(unsigned int bitIndex, bool oldValue, bool newValue)
{
    // Debug: Always log to see if callback is triggered
    wolf::logInfo("DevTools: Global flag change detected - bit %u: %d -> %d", bitIndex, oldValue, newValue);

    const auto &registry = GameStateRegistry::instance();
    const auto &globalDoc = registry.getGlobalConfig().globalGameState;

    if (!globalDoc.contains(bitIndex))
    {
        warn("BitField GlobalGameState index %u was changed from %d to %d", bitIndex, oldValue, newValue);
    }
    else
    {
        wolf::logInfo("BitField GlobalGameState index %u (%s) was changed from %d to %d", bitIndex, globalDoc.at(bitIndex).c_str(), oldValue, newValue);
    }
}

void onMapBitFieldChange(int mapId, const std::string &fieldName, const std::unordered_map<unsigned, std::string> &documentation, unsigned int bitIndex,
                         bool oldValue, bool newValue, bool showAlways = false)
{
    std::string mapName = okami::MapTypes::GetName(static_cast<okami::MapTypes::Enum>(mapId));
    std::string fullName = "(" + mapName + ") MapState::" + fieldName;

    if (!documentation.contains(bitIndex))
    {
        warn("BitField %s index %u was changed from %d to %d", fullName.c_str(), bitIndex, oldValue, newValue);
    }
    else if (showAlways)
    {
        wolf::logInfo("BitField %s index %u (%s) was changed from %d to %d", fullName.c_str(), bitIndex, documentation.at(bitIndex).c_str(), oldValue,
                      newValue);
    }
}

void onTrackerBitFieldChange(const std::string &fieldName, const std::unordered_map<unsigned, std::string> &documentation, unsigned int bitIndex, bool oldValue,
                             bool newValue, bool showAlways = false)
{
    std::string fullName = "TrackerData::" + fieldName;

    if (!documentation.contains(bitIndex))
    {
        warn("BitField %s index %u was changed from %d to %d", fullName.c_str(), bitIndex, oldValue, newValue);
    }
    else if (showAlways)
    {
        wolf::logInfo("BitField %s index %u (%s) was changed from %d to %d", fullName.c_str(), bitIndex, documentation.at(bitIndex).c_str(), oldValue,
                      newValue);
    }
}

void onWorldStateBitFieldChange(const std::string &fieldName, const std::unordered_map<unsigned, std::string> &documentation, unsigned int bitIndex,
                                bool oldValue, bool newValue, bool showAlways = false)
{
    std::string fullName = "WorldStateData::" + fieldName;

    if (!documentation.contains(bitIndex))
    {
        warn("BitField %s index %u was changed from %d to %d", fullName.c_str(), bitIndex, oldValue, newValue);
    }
    else if (showAlways)
    {
        wolf::logInfo("BitField %s index %u (%s) was changed from %d to %d", fullName.c_str(), bitIndex, documentation.at(bitIndex).c_str(), oldValue,
                      newValue);
    }
}

// Memory watchers for non-BitField data
void setupMemoryWatchers()
{
    // Memory accessors should already be initialized by initializeMonitors()
    // But ensure they're initialized just in case
    if (!AmmyStats.is_bound())
    {
        initializeMemoryAccessors();
    }

    // Watch CharacterStats unknown fields
    wolf::watchMemory(
        AmmyStats.raw() + offsetof(okami::CharacterStats, unk1), sizeof(uint32_t),
        [](uintptr_t address, const void *oldData, const void *newData, size_t size)
        {
            uint32_t oldVal = *static_cast<const uint32_t *>(oldData);
            uint32_t newVal = *static_cast<const uint32_t *>(newData);
            warn("CharacterStats::unk1 was changed from %08X to %08X", oldVal, newVal);
        },
        "CharacterStats::unk1");

    wolf::watchMemory(
        AmmyStats.raw() + offsetof(okami::CharacterStats, unk1b), sizeof(uint32_t),
        [](uintptr_t address, const void *oldData, const void *newData, size_t size)
        {
            uint32_t oldVal = *static_cast<const uint32_t *>(oldData);
            uint32_t newVal = *static_cast<const uint32_t *>(newData);
            warn("CharacterStats::unk1b was changed from %08X to %08X", oldVal, newVal);
        },
        "CharacterStats::unk1b");

    // Watch CollectionData unknown fields
    wolf::watchMemory(
        AmmyCollections.raw() + offsetof(okami::CollectionData, unk1), sizeof(uint32_t),
        [](uintptr_t address, const void *oldData, const void *newData, size_t size)
        {
            uint32_t oldVal = *static_cast<const uint32_t *>(oldData);
            uint32_t newVal = *static_cast<const uint32_t *>(newData);
            warn("CollectionData::unk1 was changed from %08X to %08X", oldVal, newVal);
        },
        "CollectionData::unk1");

    // Watch TrackerData unknown fields
    wolf::watchMemory(
        AmmyTracker.raw() + offsetof(okami::TrackerData, field_52), sizeof(uint16_t),
        [](uintptr_t address, const void *oldData, const void *newData, size_t size)
        {
            uint16_t oldVal = *static_cast<const uint16_t *>(oldData);
            uint16_t newVal = *static_cast<const uint16_t *>(newData);
            warn("TrackerData::field_52 was changed from %04X to %04X", oldVal, newVal);
        },
        "TrackerData::field_52");

    // Watch WorldStateData unknown fields (example)
    wolf::watchMemory(
        AmmyCollections.raw() + offsetof(okami::CollectionData, world) + offsetof(okami::WorldStateData, unk1), sizeof(uint32_t),
        [](uintptr_t address, const void *oldData, const void *newData, size_t size)
        {
            uint32_t oldVal = *static_cast<const uint32_t *>(oldData);
            uint32_t newVal = *static_cast<const uint32_t *>(newData);
            warn("WorldStateData::unk1 was changed from %08X to %08X", oldVal, newVal);
        },
        "WorldStateData::unk1");
}

void initializeMonitors()
{
    // Initialize memory accessors first before creating monitors
    initializeMemoryAccessors();

    const auto &registry = GameStateRegistry::instance();

    // Global flags monitor
    globalFlagsMonitor = wolf::createBitfieldMonitor(GlobalGameStateFlags.raw(), (86 + 7) / 8, onGlobalFlagChange, "Global Game State Flags");

    // MapState monitors for all maps
    for (int mapId = 0; mapId < okami::MapTypes::NUM_MAP_TYPES; mapId++)
    {
        const auto &mapConfig = registry.getMapConfig(static_cast<okami::MapTypes::Enum>(mapId));
        uintptr_t mapStateBaseAddr = MapData.raw() + (mapId * sizeof(okami::MapState));

        // collectedObjects
        collectedObjectsMonitors[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, collectedObjects), (96 + 7) / 8,
            [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "collectedObjects", mapConfig.collectedObjects, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " collectedObjects").c_str());

        // commonStates
        commonStatesMonitors[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, commonStates), (32 + 7) / 8,
            [mapId](unsigned int bitIndex, bool oldValue, bool newValue)
            {
                const auto &registry = GameStateRegistry::instance();
                const auto &commonStates = registry.getGlobalConfig().commonStates;
                onMapBitFieldChange(mapId, "commonStates", commonStates, bitIndex, oldValue, newValue);
            },
            ("Map " + std::to_string(mapId) + " commonStates").c_str());

        // areasRestored
        areasRestoredMonitors[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, areasRestored), (96 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "areasRestored", mapConfig.areasRestored, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " areasRestored").c_str());

        // treesBloomed
        treesBloomedMonitors[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, treesBloomed), (32 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "treesBloomed", mapConfig.treesBloomed, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " treesBloomed").c_str());

        // cursedTreesBloomed
        cursedTreesBloomedMonitors[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, cursedTreesBloomed), (32 + 7) / 8,
            [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "cursedTreesBloomed", mapConfig.cursedTreesBloomed, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " cursedTreesBloomed").c_str());

        // fightsCleared
        fightsCleared[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, fightsCleared), (128 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "fightsCleared", mapConfig.fightsCleared, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " fightsCleared").c_str());

        // npcHasMoreToSay
        npcHasMoreToSay[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, npcHasMoreToSay), (64 + 7) / 8,
            [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "npcHasMoreToSay", mapConfig.npcs, bitIndex, oldValue, newValue, true); },
            ("Map " + std::to_string(mapId) + " npcHasMoreToSay").c_str());

        // npcUnknown
        npcUnknown[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, npcUnknown), (64 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "npcUnknown", mapConfig.npcs, bitIndex, oldValue, newValue, true); },
            ("Map " + std::to_string(mapId) + " npcUnknown").c_str());

        // mapsExplored
        mapsExplored[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, mapsExplored), (64 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "mapsExplored", mapConfig.mapsExplored, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " mapsExplored").c_str());

        // field_DC
        field_DC[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, field_DC), (32 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "field_DC", mapConfig.field_DC, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " field_DC").c_str());

        // field_E0
        field_E0[mapId] = wolf::createBitfieldMonitor(
            mapStateBaseAddr + offsetof(okami::MapState, field_E0), (32 + 7) / 8, [mapId, &mapConfig](unsigned int bitIndex, bool oldValue, bool newValue)
            { onMapBitFieldChange(mapId, "field_E0", mapConfig.field_E0, bitIndex, oldValue, newValue); },
            ("Map " + std::to_string(mapId) + " field_E0").c_str());
    }

    // TrackerData monitors
    trackerGameProgressionMonitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, gameProgressionBits), (96 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto gameProgress = registry.getGlobalConfig().gameProgress;
            onTrackerBitFieldChange("gameProgressionBits", gameProgress, bitIndex, oldValue, newValue, true);
        },
        "TrackerData gameProgressionBits");

    trackerAnimalsFedFirstTimeMonitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, animalsFedFirstTime), (64 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto animalsFedFirstTime = registry.getGlobalConfig().animalsFedFirstTime;
            onTrackerBitFieldChange("animalsFedFirstTime", animalsFedFirstTime, bitIndex, oldValue, newValue);
        },
        "TrackerData animalsFedFirstTime");

    trackerField34Monitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, field_34), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const std::unordered_map<unsigned, std::string> emptyDoc;
            onTrackerBitFieldChange("field_34", emptyDoc, bitIndex, oldValue, newValue);
        },
        "TrackerData field_34");

    trackerField38Monitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, field_38), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const std::unordered_map<unsigned, std::string> emptyDoc;
            onTrackerBitFieldChange("field_38", emptyDoc, bitIndex, oldValue, newValue);
        },
        "TrackerData field_38");

    trackerBrushUpgradesMonitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, brushUpgrades), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto brushUpgrades = registry.getGlobalConfig().brushUpgrades;
            onTrackerBitFieldChange("brushUpgrades", brushUpgrades, bitIndex, oldValue, newValue);
        },
        "TrackerData brushUpgrades");

    trackerOptionFlagsMonitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, optionFlags), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const std::unordered_map<unsigned, std::string> emptyDoc;
            onTrackerBitFieldChange("optionFlags", emptyDoc, bitIndex, oldValue, newValue);
        },
        "TrackerData optionFlags");

    trackerAreasRestoredMonitor = wolf::createBitfieldMonitor(
        AmmyTracker.raw() + offsetof(okami::TrackerData, areasRestored), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto areasRestored = registry.getGlobalConfig().areasRestored;
            onTrackerBitFieldChange("areasRestored", areasRestored, bitIndex, oldValue, newValue);
        },
        "TrackerData areasRestored");

    // WorldStateData monitors
    worldKeyItemsAcquiredMonitor = wolf::createBitfieldMonitor(
        AmmyCollections.raw() + offsetof(okami::CollectionData, world) + offsetof(okami::WorldStateData, keyItemsAcquired), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto keyItemsFound = registry.getGlobalConfig().keyItemsFound;
            onWorldStateBitFieldChange("keyItemsAcquired", keyItemsFound, bitIndex, oldValue, newValue);
        },
        "WorldStateData keyItemsAcquired");

    worldGoldDustsAcquiredMonitor = wolf::createBitfieldMonitor(
        AmmyCollections.raw() + offsetof(okami::CollectionData, world) + offsetof(okami::WorldStateData, goldDustsAcquired), (32 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto &goldDustsFound = registry.getGlobalConfig().goldDustsFound;
            onWorldStateBitFieldChange("goldDustsAcquired", goldDustsFound, bitIndex, oldValue, newValue);
        },
        "WorldStateData goldDustsAcquired");

    for (int i = 0; i < okami::MapTypes::NUM_MAP_TYPES + 1; i++)
    {
        worldMapStateBitsMonitors[i] = wolf::createBitfieldMonitor(
            AmmyCollections.raw() + offsetof(okami::CollectionData, world) + offsetof(okami::WorldStateData, mapStateBits) + (i * sizeof(okami::BitField<256>)),
            (256 + 7) / 8,
            [i](unsigned int bitIndex, bool oldValue, bool newValue)
            {
                const auto &registry = GameStateRegistry::instance();
                auto mapType = static_cast<okami::MapTypes::Enum>(i);
                const auto &worldStateBits = registry.getMapConfig(mapType).worldStateBits;
                std::string fieldName = "mapStateBits[" + std::to_string(i) + "] (" + okami::MapTypes::GetName(i) + ")";
                onWorldStateBitFieldChange(fieldName, worldStateBits, bitIndex, oldValue, newValue, true);
            },
            ("WorldStateData mapStateBits[" + std::to_string(i) + "]").c_str());
    }

    worldAnimalsFedBitsMonitor = wolf::createBitfieldMonitor(
        AmmyCollections.raw() + offsetof(okami::CollectionData, world) + offsetof(okami::WorldStateData, animalsFedBits), (256 + 7) / 8,
        [](unsigned int bitIndex, bool oldValue, bool newValue)
        {
            const auto &registry = GameStateRegistry::instance();
            const auto &animalsFound = registry.getGlobalConfig().animalsFound;
            onWorldStateBitFieldChange("animalsFedBits", animalsFound, bitIndex, oldValue, newValue);
        },
        "WorldStateData animalsFedBits");
}

} // anonymous namespace

void initializeDevDataFinder()
{
    if (initialized)
    {
        wolf::logWarning("DevTools: Data finder already initialized");
        return;
    }

    // Initialize BitField monitors
    initializeMonitors();

    // Initialize memory watchers for non-BitField data
    setupMemoryWatchers();

    // Register cleanup handler
    wolf::registerCleanupHandler([]() { shutdownDevDataFinder(); });

    initialized = true;
}

void shutdownDevDataFinder()
{
    // WOLF framework handles monitor cleanup
    initialized = false;
}
