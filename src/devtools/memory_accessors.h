#pragma once

#include <okami/structs.hpp>

#include "wolf_framework.hpp"

/**
 * @brief Memory accessors for Okami game structures using WOLF framework
 *
 * These provide the same interface as the unified mod but using WOLF's memory access system.
 * Offsets are from main.dll base address as determined from the unified mod.
 */

// Memory offsets from unified mod memorymap.cpp
constexpr uintptr_t AMMY_STATS_OFFSET = 0xB4DF90;
constexpr uintptr_t AMMY_COLLECTIONS_OFFSET = 0xB205D0;
constexpr uintptr_t AMMY_TRACKER_OFFSET = 0xB21780;
constexpr uintptr_t MAP_DATA_OFFSET = 0xB322B0;
constexpr uintptr_t GLOBAL_GAME_STATE_FLAGS_OFFSET = 0xB6B2AC;
constexpr uintptr_t AMMY_USABLE_BRUSHES_OFFSET = 0xB6B2BC;
constexpr uintptr_t AMMY_OBTAINED_BRUSHES_OFFSET = 0xB6B2C0;
constexpr uintptr_t AMMY_BRUSH_UPGRADES_OFFSET = 0xB6B2C4;
constexpr uintptr_t AMMY_MODEL_OFFSET = 0xBB68B8;
constexpr uintptr_t CURRENT_MAP_ID_OFFSET = 0xB20600;
constexpr uintptr_t EXTERIOR_MAP_ID_OFFSET = 0xB205FC;

// Global memory accessors (initialized once)
inline wolf::MemoryAccessor<okami::CharacterStats> AmmyStats;
inline wolf::MemoryAccessor<okami::CollectionData> AmmyCollections;
inline wolf::MemoryAccessor<okami::TrackerData> AmmyTracker;
inline wolf::MemoryAccessor<std::array<okami::MapState, okami::MapTypes::NUM_MAP_TYPES>> MapData;
inline wolf::MemoryAccessor<okami::BitField<86>> GlobalGameStateFlags;
inline wolf::MemoryAccessor<okami::BitField<32>> AmmyUsableBrushes;
inline wolf::MemoryAccessor<okami::BitField<32>> AmmyObtainedBrushes;
inline wolf::MemoryAccessor<std::array<uint8_t, 64>> AmmyBrushUpgrades;
inline wolf::MemoryAccessor<uint32_t> CurrentMapID;
inline wolf::MemoryAccessor<uint32_t> ExteriorMapID;

/**
 * @brief Initialize all memory accessors
 *
 * Call this once during mod initialization to set up memory access.
 */
inline void initializeMemoryAccessors()
{
    AmmyStats.bind("main.dll", AMMY_STATS_OFFSET);
    AmmyCollections.bind("main.dll", AMMY_COLLECTIONS_OFFSET);
    AmmyTracker.bind("main.dll", AMMY_TRACKER_OFFSET);
    MapData.bind("main.dll", MAP_DATA_OFFSET);
    GlobalGameStateFlags.bind("main.dll", GLOBAL_GAME_STATE_FLAGS_OFFSET);
    AmmyUsableBrushes.bind("main.dll", AMMY_USABLE_BRUSHES_OFFSET);
    AmmyObtainedBrushes.bind("main.dll", AMMY_OBTAINED_BRUSHES_OFFSET);
    AmmyBrushUpgrades.bind("main.dll", AMMY_BRUSH_UPGRADES_OFFSET);
    CurrentMapID.bind("main.dll", CURRENT_MAP_ID_OFFSET);
    ExteriorMapID.bind("main.dll", EXTERIOR_MAP_ID_OFFSET);
}
