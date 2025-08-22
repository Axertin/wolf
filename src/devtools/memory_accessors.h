#pragma once

#include <okami/structs.hpp>

#include "wolf_framework.hpp"

/**
 * @brief Memory accessors for Okami game structures using WOLF framework
 *
 * These provide the same interface as the unified mod but using WOLF's memory access system.
 * Offsets are from main.dll base address as determined from the unified mod.
 */

// Global memory accessors (initialized once)
inline wolf::MemoryAccessor<okami::CharacterStats> AmmyStats;
inline wolf::MemoryAccessor<okami::CollectionData> AmmyCollections;
inline wolf::MemoryAccessor<okami::TrackerData> AmmyTracker;
inline wolf::MemoryAccessor<std::array<okami::MapState, okami::MapTypes::NUM_MAP_TYPES>> MapData;
inline wolf::MemoryAccessor<std::array<okami::BitField<512>, okami::MapTypes::NUM_MAP_TYPES>> DialogBits;
inline wolf::MemoryAccessor<okami::BitField<86>> GlobalGameStateFlags;
inline wolf::MemoryAccessor<okami::BitField<32>> AmmyUsableBrushes;
inline wolf::MemoryAccessor<okami::BitField<32>> AmmyObtainedBrushes;
inline wolf::MemoryAccessor<std::array<uint8_t, 64>> AmmyBrushUpgrades; // why are there even 64 wtf??
inline wolf::MemoryAccessor<uint32_t> CurrentMapID;
inline wolf::MemoryAccessor<uint32_t> ExteriorMapID;
inline wolf::MemoryAccessor<uint32_t> VestigialMapID1; // No idea why this exists
inline wolf::MemoryAccessor<uint32_t> VestigialMapID2; // No idea why this exists
inline wolf::MemoryAccessor<std::array<okami::ItemParam, okami::ItemTypes::NUM_ITEM_TYPES>> ItemParams;

inline wolf::MemoryAccessor<okami::cAmmyModel *> AmmyModel;
inline wolf::MemoryAccessor<int32_t> BrushTargetInk;
inline wolf::MemoryAccessor<int32_t> BrushCurrentInk;
inline wolf::MemoryAccessor<uint8_t> InkUpgradeCount;
inline wolf::MemoryAccessor<uint8_t> CanvasBrushStrokes;
inline wolf::MemoryAccessor<uint8_t> FoodUpgradeCount;
inline wolf::MemoryAccessor<uint32_t> AmmyDisplayedMoney;
inline wolf::MemoryAccessor<uint32_t> AmmyMaxMoney; // TODO: Make Array of length 4
inline wolf::MemoryAccessor<int16_t> PraiseUpgradeBar;
inline wolf::MemoryAccessor<int16_t> HealthUpgradeBar;
inline wolf::MemoryAccessor<int16_t> FoodUpgradeBar;
inline wolf::MemoryAccessor<int16_t> MoneyUpgradeBar;
inline wolf::MemoryAccessor<int16_t> InkUpgradeBar;

inline wolf::MemoryAccessor<float> CameraFOV;

/**
 * @brief Initialize all memory accessors
 *
 * Call this once during mod initialization to set up memory access.
 */
inline void initializeMemoryAccessors()
{
    // everything needed for a save file vvvvvvvvv
    AmmyStats.bind("main.dll", 0xB4DF90);
    AmmyCollections.bind("main.dll", 0xB205D0);
    AmmyTracker.bind("main.dll", 0xB21780);
    MapData.bind("main.dll", 0xB322B0);
    DialogBits.bind("main.dll", 0xB36CF0);
    // everything needed for a save file ^^^^^^^^

    GlobalGameStateFlags.bind("main.dll", 0xB6B2AC);
    AmmyUsableBrushes.bind("main.dll", 0x890A30);
    AmmyObtainedBrushes.bind("main.dll", 0x890A38);
    AmmyBrushUpgrades.bind("main.dll", 0x8909C0 + 0x80);

    ItemParams.bind("main.dll", 0x7AB220);
    // NOTE: this gets computed via max food value in CharacterStats and is
    // strictly used for inventory
    FoodUpgradeCount.bind("main.dll", 0xB1F207);
    BrushTargetInk.bind("main.dll", 0x8928A4);
    BrushCurrentInk.bind("main.dll", 0x8928A8);
    InkUpgradeCount.bind("main.dll", 0xB1F208);
    CanvasBrushStrokes.bind("main.dll", 0x888C58);

    CurrentMapID.bind("main.dll", 0xB65E74);
    ExteriorMapID.bind("main.dll", 0xB6B240);
    VestigialMapID1.bind("main.dll", 0xB4F0B4);
    VestigialMapID2.bind("main.dll", 0xB6B246);

    CameraFOV.bind("main.dll", 0xB663B0);

    AmmyModel.bind("main.dll", 0xB6B2D0);
}
