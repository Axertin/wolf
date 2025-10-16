#include "devtools_gui.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <okami/animals.hpp>
#include <okami/bestiarytome.hpp>
#include <okami/brushtype.hpp>
#include <okami/dojotech.hpp>
#include <okami/fish.hpp>
#include <okami/itemtype.hpp>
#include <okami/logbook.hpp>
#include <okami/maptype.hpp>
#include <okami/movelisttome.hpp>
#include <okami/straybeads.hpp>
#include <okami/structs.hpp>
#include <okami/travelguides.hpp>
#include <okami/treasures.hpp>

#include "frametimer.h"
#include "gamestateregistry.h"
#include "memory_accessors.h"
#include "wolf_framework.hpp"

// Frame timing
static FrameTimer frameTimer;
static bool showDevToolsWindow = true;

// Item categories for inventory management
static const std::vector<uint16_t> consumableItems = {okami::ItemTypes::FeedbagFish,  okami::ItemTypes::FeedbagHerbs, okami::ItemTypes::FeedbagMeat,
                                                      okami::ItemTypes::FeedbagSeeds, okami::ItemTypes::MermaidCoin,  okami::ItemTypes::DemonFang,
                                                      okami::ItemTypes::GoldDust};

static const std::vector<uint16_t> treasureItems = {
    // Add treasure item IDs when available
};

static const std::vector<uint16_t> keyItems = {
    // Add key item IDs when available
};

// Weapon data from original devtools
static const std::unordered_map<unsigned, std::string> weaponSlotNames = {
    {0x00, "Divine Retribution"}, {0x01, "Snarling Beast"},     {0x02, "Infinity Judge"}, {0x03, "Trinity Mirror"},
    {0x04, "Solar Flare"},        {0x10, "Tsumugari"},          {0x11, "Seven Strike"},   {0x12, "Blade of Kusanagi"},
    {0x13, "Eighth Wonder"},      {0x14, "Thunder Edge"},       {0x20, "Devout Beads"},   {0x21, "Life Beads"},
    {0x22, "Exorcism Beads"},     {0x23, "Resurrection Beads"}, {0x24, "Tundra Beads"},   {0xFF, "None"},
};

static const std::vector<std::pair<uint16_t, uint8_t>> weaponsList = {
    {okami::ItemTypes::DivineRetribution, 0x00}, {okami::ItemTypes::SnarlingBeast, 0x01},     {okami::ItemTypes::InfinityJudge, 0x02},
    {okami::ItemTypes::TrinityMirror, 0x03},     {okami::ItemTypes::SolarFlare, 0x04},        {okami::ItemTypes::Tsumugari, 0x10},
    {okami::ItemTypes::SevenStrike, 0x11},       {okami::ItemTypes::BladeOfKusanagi, 0x12},   {okami::ItemTypes::EighthWonder, 0x13},
    {okami::ItemTypes::ThunderEdge, 0x14},       {okami::ItemTypes::DevoutBeads, 0x20},       {okami::ItemTypes::LifeBeads, 0x21},
    {okami::ItemTypes::ExorcismBeads, 0x22},     {okami::ItemTypes::ResurrectionBeads, 0x23}, {okami::ItemTypes::TundraBeads, 0x24},
};

/**
 * @brief RAII helper for collapsing header groups with indentation
 */
class IndentedGroup
{
  private:
    int amount;
    bool result = false;

  public:
    IndentedGroup(const char *name, ImGuiTreeNodeFlags flags = 0, int amt = 6) : amount(amt)
    {
        result = ImGui::CollapsingHeader(name, flags);
        ImGui::PushID(name);
        ImGui::Indent(static_cast<float>(amount));
    }

    ~IndentedGroup()
    {
        ImGui::PopID();
        ImGui::Indent(static_cast<float>(-amount));
    }

    operator bool() const
    {
        return result;
    }
};

/**
 * @brief Convenience macro for IndentedGroup
 */
#define GROUP(name) if (auto _group_##__LINE__ = IndentedGroup(name))

/**
 * @brief Render a stat with current/total values
 */
static void drawStatPair(const char *name, int type, void *pCurrent, void *pTotal)
{
    ImGui::PushID(name);

    ImGui::Text("%s:", name);
    ImGui::SameLine();
    ImGui::SetCursorPosX(100);
    ImGui::SetNextItemWidth(80);
    ImGui::InputScalar("##current", type, pCurrent);

    ImGui::SameLine();
    ImGui::Text("/");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputScalar("##total", type, pTotal);

    ImGui::PopID();
}

/**
 * @brief Render a single stat value
 */
static void drawStat(const char *name, int type, void *pCurrent)
{
    ImGui::PushID(name);

    ImGui::Text("%s:", name);
    ImGui::SameLine();
    ImGui::SetCursorPosX(100);
    ImGui::SetNextItemWidth(80);
    ImGui::InputScalar("##current", type, pCurrent);

    ImGui::PopID();
}

/**
 * @brief Render a checkbox for a specific bit in a BitField
 */
template <unsigned int N> static void checkboxBitField(const char *label, unsigned idx, okami::BitField<N> &bits)
{
    ImGui::PushID(idx);
    ImGui::CheckboxFlags(label, bits.GetIdxPtr(idx), bits.GetIdxMask(idx));
    ImGui::SetItemTooltip("%d (0x%X): %s", idx, idx, label);
    ImGui::PopID();
}

/**
 * @brief Helper to render multiple checkboxes in columns with All/None buttons
 */
template <unsigned int N> static void checklistCols(const char *groupName, unsigned numCols, const auto &pNameFn, okami::BitField<N> &bits)
{
    ImGui::PushID(&bits);
    if (groupName ? ImGui::CollapsingHeader(groupName) : true)
    {
        ImGui::Indent(6);
        if (ImGui::Button("All##Btn"))
        {
            bits.SetAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("None##Btn"))
        {
            bits.ClearAll();
        }

        ImGui::BeginTable("TblId", numCols);

        unsigned rows = (bits.count + numCols - 1) / numCols;
        for (unsigned i = 0; i < rows; i++)
        {
            for (unsigned j = 0; j < numCols; j++)
            {
                ImGui::TableNextColumn();
                unsigned index = i + j * rows;
                if (index < bits.count)
                {
                    checkboxBitField(pNameFn(index), index, bits);
                }
                else
                {
                    ImGui::Text("");
                }
            }
        }
        ImGui::EndTable();
        ImGui::Indent(-6);
        if (groupName)
        {
            ImGui::Separator();
        }
    }
    ImGui::PopID();
}

/**
 * @brief Helper for collections with collected/viewed states
 */
template <unsigned int N> static void checklistColsTome(const char *groupName, const auto &pNameFn, okami::BitField<N> &collected, okami::BitField<N> &viewed)
{
    GROUP(groupName)
    {
        if (ImGui::Button("All##Btn"))
        {
            collected.SetAll();
            viewed.SetAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("None##Btn"))
        {
            collected.ClearAll();
            viewed.ClearAll();
        }

        ImGui::BeginTable("TblId", 3);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHeaderLabel);
        ImGui::TableSetupColumn("Col.");
        ImGui::TableSetupColumn("Read");
        ImGui::TableHeadersRow();

        for (unsigned i = 0; i < N; i++)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            const char *itemName = pNameFn(i);

            ImGui::TableNextColumn();
            ImGui::Text("%s", itemName);

            ImGui::TableNextColumn();
            checkboxBitField("##Collected", i, collected);

            ImGui::TableNextColumn();
            checkboxBitField("##Viewed", i, viewed);
            ImGui::PopID();
        }

        ImGui::EndTable();
        ImGui::Separator();
    }
}

/**
 * @brief Helper to render checkboxes in columns with mapped names and All/None buttons
 */
template <unsigned int N>
static void checklistColsMapped(const char *groupName, unsigned numCols, const char *basename, okami::BitField<N> &bits,
                                const std::unordered_map<unsigned, std::string> &mapping)
{
    std::string mem;
    auto NameFn = [&](unsigned id) -> const char *
    {
        if (mapping.count(id))
        {
            return mapping.at(id).c_str();
        }
        mem = basename + std::to_string(id);
        return mem.c_str();
    };
    checklistCols(groupName, numCols, NameFn, bits);
}

/**
 * @brief Helper to render map-specific state flags with registry descriptions
 * Shows flags in a clean grid with descriptions in tooltips
 */
template <unsigned int N> static void checklistManyMapped(const char *groupName, const char *category, okami::MapTypes::Enum map, okami::BitField<N> &bits)
{
    GROUP(groupName)
    {
        auto &registry = GameStateRegistry::instance();

        for (unsigned i = 0; i < N; i++)
        {
            ImGui::PushID(i);

            auto description = registry.getMapDescription(map, category, i);

            // Always use empty label for clean grid
            ImGui::CheckboxFlags("##bit", bits.GetIdxPtr(i), bits.GetIdxMask(i));

            // Tooltip with description and index
            if (!description.empty())
            {
                ImGui::SetItemTooltip("%d (0x%X): %s", i, i, description.data());
            }
            else
            {
                ImGui::SetItemTooltip("%d (0x%X)", i, i);
            }

            // Grid layout: 8 per row
            if ((i + 1) % 8 != 0)
            {
                ImGui::SameLine();
            }

            ImGui::PopID();
        }
    }
}

/**
 * @brief Render detailed state for a specific map
 */
static void mapGroup(unsigned mapIdx)
{
    auto &registry = GameStateRegistry::instance();
    auto *ammyCollections = AmmyCollections.get_ptr();
    auto *mapData = MapData.get_ptr();

    if (!ammyCollections || !mapData)
        return;

    okami::MapState &mapState = (*mapData)[mapIdx];
    auto mapEnum = static_cast<okami::MapTypes::Enum>(mapIdx);

    checklistManyMapped("Event Bits", "worldStateBits", mapEnum, ammyCollections->world.mapStateBits[mapIdx]);
    checklistManyMapped("Collected Objects", "collectedObjects", mapEnum, mapState.collectedObjects);
    checklistManyMapped("Areas Restored", "areasRestored", mapEnum, mapState.areasRestored);
    checklistManyMapped("Trees Bloomed", "treesBloomed", mapEnum, mapState.treesBloomed);
    checklistManyMapped("Cursed Trees Bloomed", "cursedTreesBloomed", mapEnum, mapState.cursedTreesBloomed);
    checklistManyMapped("Fights Cleared", "fightsCleared", mapEnum, mapState.fightsCleared);
    checklistManyMapped("Maps Explored", "mapsExplored", mapEnum, mapState.mapsExplored);
    checklistManyMapped("NPC Has More to Say", "npcs", mapEnum, mapState.npcHasMoreToSay);
    checklistManyMapped("NPC Unknown", "npcs", mapEnum, mapState.npcUnknown);

    GROUP("Custom Data")
    {
        for (unsigned i = 0; i < 32; i++)
        {
            ImGui::PushID(i);
            auto description = registry.getMapDescription(mapEnum, "userIndices", i);
            std::string name = !description.empty() ? std::string(description) : std::to_string(i);
            ImGui::Text("%08X", mapState.user[i]);
            ImGui::SameLine();
            ImGui::InputScalar(name.c_str(), ImGuiDataType_U32, &mapState.user[i]);
            ImGui::SetItemTooltip("%d (0x%X)", i, i);
            ImGui::PopID();
        }
    }

    checklistManyMapped("Unknown DC", "field_DC", mapEnum, mapState.field_DC);
    checklistManyMapped("Unknown E0", "field_E0", mapEnum, mapState.field_E0);
}

/**
 * @brief Draw inventory section by category
 */
static void drawInventory(const char *categoryName, const std::vector<uint16_t> &items)
{
    ImGui::PushID(categoryName);
    ImGui::SeparatorText(categoryName);

    ImGui::BeginTable("InventoryTbl", 2);
    for (auto &i : items)
    {
        ImGui::PushID(i);
        ImGui::TableNextColumn();

        int step = 1;
        ImGui::SetNextItemWidth(80);
        auto *ammyCollections = AmmyCollections.get_ptr();
        if (ammyCollections)
        {
            ImGui::InputScalar(okami::ItemTypes::GetName(i), ImGuiDataType_U16, &ammyCollections->inventory[i], &step);
        }
        else
        {
            ImGui::Text("%s: N/A", okami::ItemTypes::GetName(i));
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopID();
}

void renderDevToolsWindow(int width, int height, float scale)
{
    frameTimer.update();
    initializeMemoryAccessors();

    WOLF_IMGUI_BEGIN(width, height, scale);

    if (ImGui::Begin("DevTools", &showDevToolsWindow))
    {
        ImGui::Text("WOLF DevTools v1.0.0");

        // Basic cheat button and timing info
        if (ImGui::Button("CHEAT ME"))
        {
            try
            {
                // Basic cheats from original devtools
                auto *ammyStats = AmmyStats.get_ptr();
                auto *ammyCollections = AmmyCollections.get_ptr();
                auto *globalFlags = GlobalGameStateFlags.get_ptr();
                auto *usableBrushes = AmmyUsableBrushes.get_ptr();
                auto *obtainedBrushes = AmmyObtainedBrushes.get_ptr();

                if (ammyStats && ammyCollections && globalFlags && usableBrushes && obtainedBrushes)
                {
                    // Allow celestial brush use at start of game
                    ammyCollections->world.mapStateBits[0].Clear(10);
                    // Skip part of cutscene
                    ammyCollections->world.mapStateBits[okami::MapTypes::KamikiVillage].Set(10);
                    // Unlock all dojo techniques and brushes
                    ammyStats->dojoTechniquesUnlocked.SetAll();
                    usableBrushes->SetAll();
                    obtainedBrushes->SetAll();
                    // Give basic supplies
                    ammyCollections->inventory[okami::ItemTypes::FeedbagFish] = 100;
                    ammyCollections->inventory[okami::ItemTypes::FeedbagHerbs] = 100;
                    ammyCollections->inventory[okami::ItemTypes::FeedbagMeat] = 100;
                    ammyCollections->inventory[okami::ItemTypes::FeedbagSeeds] = 100;
                    ammyCollections->inventory[okami::ItemTypes::MermaidCoin] = 100;
                    ammyCollections->inventory[okami::ItemTypes::DemonFang] = 100;
                    ammyCollections->inventory[okami::ItemTypes::GoldDust] = 100;
                    ammyCollections->currentMoney = 999999999;
                }
            }
            catch (...)
            {
                wolf::logWarning("DevTools: Failed to apply cheats - memory not accessible");
            }
        }
        ImGui::SameLine();

        auto *ammyTracker = AmmyTracker.get_ptr();
        if (ammyTracker)
        {
            ImGui::Text("IGT: %d", ammyTracker->timePlayed);
        }
        else
        {
            ImGui::Text("IGT: N/A");
        }

        ImGui::Text("Frame Time: %.2f ms (%.2f FPS)", frameTimer.getFrameTimeMs(), frameTimer.getFPS());

        ImGui::Separator();

        // Runtime info
        ImGui::Text("WOLF Runtime:");
        ImGui::Text("  Version: %s", wolf::getRuntimeVersion());
        ImGui::Text("  Build: %s", wolf::getRuntimeBuildInfo());

        ImGui::Separator();

        // Character Stats
        GROUP("Character Stats")
        {
            auto *ammyStats = AmmyStats.get_ptr();
            auto *ammyCollections = AmmyCollections.get_ptr();

            if (ammyStats && ammyCollections)
            {
                drawStatPair("Health", ImGuiDataType_U16, &ammyStats->currentHealth, &ammyStats->maxHealth);
                drawStatPair("Money", ImGuiDataType_U32, &ammyCollections->currentMoney, &ammyCollections->world.totalMoney);
                drawStatPair("Praise", ImGuiDataType_U16, &ammyStats->currentPraise, &ammyStats->totalPraise);
                drawStatPair("Ink", ImGuiDataType_U32, &ammyCollections->currentInk, &ammyCollections->maxInk);
                drawStatPair("Food", ImGuiDataType_U16, &ammyStats->currentFood, &ammyStats->maxFood);
                drawStat("Godhood", ImGuiDataType_U16, &ammyStats->godhood);
                drawStat("Demon Fangs", ImGuiDataType_U32, &ammyCollections->world.totalDemonFangs);
            }
            else
            {
                ImGui::Text("Character stats not accessible");
            }
        }

        // Collections
        GROUP("Collections")
        {
            auto *ammyCollections = AmmyCollections.get_ptr();
            if (ammyCollections)
            {
                ImGui::Text("Current Map: %d", ammyCollections->currentMapId);
                ImGui::Text("Last Map: %d", ammyCollections->lastMapId);
                ImGui::Text("Wallet Upgrades: %d", ammyCollections->walletUpgrades);
                ImGui::Text("Health Upgrades: %d", ammyCollections->healthUpgrades);

                checklistCols("Stray Beads", 5, okami::StrayBeads::GetName, ammyCollections->strayBeadsCollected);
                checklistColsTome("Travel Guides", okami::TravelGuides::GetName, ammyCollections->travelGuidesCollected, ammyCollections->travelGuidesViewed);
                checklistColsTome("Move List", okami::MoveListTome::GetName, ammyCollections->dojoMovesCollected, ammyCollections->dojoMovesViewed);
                checklistColsTome("Fish Tome", okami::FishTome::GetName, ammyCollections->fishTomesCollected, ammyCollections->fishTomesViewed);
                checklistColsTome("Animal Tome", okami::Animals::GetName, ammyCollections->animalTomesCollected, ammyCollections->animalTomesViewed);
                checklistColsTome("Treasure Tome", okami::Treasures::GetName, ammyCollections->treasureTomesCollected, ammyCollections->treasureTomesViewed);
            }
            else
            {
                ImGui::Text("Collections not accessible");
            }
        }

        // World State
        GROUP("World State")
        {
            auto *ammyCollections = AmmyCollections.get_ptr();
            if (ammyCollections)
            {
                drawStat("Time of Day", ImGuiDataType_U32, &ammyCollections->world.timeOfDay);
                drawStat("Day", ImGuiDataType_U16, &ammyCollections->world.day);

                GROUP("Animals Fed")
                {
                    for (unsigned animalIdx = 0; animalIdx < okami::Animals::NUM_ANIMALS && animalIdx < 32; animalIdx++)
                    {
                        drawStat(okami::Animals::GetName(animalIdx), ImGuiDataType_U16, &ammyCollections->world.numAnimalsFed[animalIdx]);
                    }
                }

                ImGui::Text("Total Enemies Killed: %u", ammyCollections->world.totalEnemiesKilled);
            }
            else
            {
                ImGui::Text("World state not accessible");
            }
        }

        // Brushes
        GROUP("Brushes")
        {
            auto *usableBrushes = AmmyUsableBrushes.get_ptr();
            auto *obtainedBrushes = AmmyObtainedBrushes.get_ptr();
            auto *brushUpgrades = AmmyBrushUpgrades.get_ptr();

            if (usableBrushes && obtainedBrushes)
            {
                checklistCols("Usable Brushes", 2, okami::BrushTypes::GetName, *usableBrushes);
                checklistCols("Obtained Brushes", 2, okami::BrushTypes::GetName, *obtainedBrushes);

                if (brushUpgrades)
                {
                    GROUP("Brush Upgrades")
                    {
                        for (int brushIdx = 0; brushIdx < 32 && brushIdx < 64; brushIdx++)
                        {
                            std::string name = "BrushUpg" + std::to_string(brushIdx);
                            drawStat(name.c_str(), ImGuiDataType_U8, &(*brushUpgrades)[brushIdx]);
                        }
                    }
                }
            }
            else
            {
                ImGui::Text("Brush data not accessible");
            }
        }

        // Maps
        GROUP("Maps")
        {
            auto currentMapId = CurrentMapID.get();
            auto exteriorMapId = ExteriorMapID.get();

            ImGui::Text("Current Map: %d", currentMapId);
            ImGui::Text("Exterior Map: %d", exteriorMapId);

            static int selectedMapId = currentMapId;
            ImGui::InputInt("Map ID", &selectedMapId);
            ImGui::SameLine();
            if (ImGui::Button("Teleport"))
            {
                auto *globalFlags = GlobalGameStateFlags.get_ptr();
                if (globalFlags)
                {
                    ExteriorMapID.set(selectedMapId);
                    CurrentMapID.set(selectedMapId);
                    globalFlags->Set(6);
                    wolf::logInfo("DevTools: Teleporting to map %d", selectedMapId);
                }
            }
        }

        // Items & Inventory
        GROUP("Items")
        {
            GROUP("Inventory")
            {
                drawInventory("Consumables", consumableItems);
            }
        }

        // Tracker Data
        GROUP("Tracker")
        {
            auto *ammyTracker = AmmyTracker.get_ptr();
            if (ammyTracker)
            {
                ImGui::Text("Game Over Count: %d", ammyTracker->gameOverCount);
                ImGui::Text("BGM Volume: %d", ammyTracker->volumeBGM);
                ImGui::Text("SE Volume: %d", ammyTracker->volumeSE);
                ImGui::Text("Voice Volume: %d", ammyTracker->volumeVoice);

                checklistColsTome("Bestiary Tome", okami::BestiaryTome::GetName, ammyTracker->bestiaryTomeUnlocked, ammyTracker->bestiaryTomeRead);
                checklistCols("Maps Visited", 2, okami::MapTypes::GetName, ammyTracker->areaVisitedFlags);
            }
            else
            {
                ImGui::Text("Tracker data not accessible");
            }
        }

        // Global Game State
        try
        {
            auto &registry = GameStateRegistry::instance();
            auto &globalConfig = registry.getGlobalConfig();
            auto *globalFlags = GlobalGameStateFlags.get_ptr();
            auto *ammyCollections = AmmyCollections.get_ptr();
            auto *ammyTracker = AmmyTracker.get_ptr();

            if (globalFlags)
            {
                checklistColsMapped("Global Game State", 2, "Gl", *globalFlags, globalConfig.globalGameState);
            }

            if (ammyCollections)
            {
                checklistColsMapped("Common States", 2, "Common", ammyCollections->world.mapStateBits[0], globalConfig.commonStates);
                checklistColsMapped("Key Items Found", 2, "KeyItem", ammyCollections->world.keyItemsAcquired, globalConfig.keyItemsFound);
                checklistColsMapped("Gold Dusts Found", 2, "GoldDust", ammyCollections->world.goldDustsAcquired, globalConfig.goldDustsFound);
                checklistColsMapped("Animals Found", 2, "Animal", ammyCollections->world.animalsFedBits, globalConfig.animalsFound);
            }

            if (ammyTracker)
            {
                checklistColsMapped("Game Progress", 2, "Prog", ammyTracker->gameProgressionBits, globalConfig.gameProgress);
                checklistColsMapped("Brush Upgrades", 2, "BrushUpg", ammyTracker->brushUpgrades, globalConfig.brushUpgrades);
                checklistColsMapped("Areas Restored", 2, "Area", ammyTracker->areasRestored, globalConfig.areasRestored);
                checklistColsMapped("Animals Fed First Time", 2, "AnimalFed", ammyTracker->animalsFedFirstTime, globalConfig.animalsFedFirstTime);
            }
        }
        catch (...)
        {
            ImGui::Text("Global Game State: Not accessible");
        }

        // Map State
        try
        {
            auto currentMapId = CurrentMapID.get();
            int currentMapIndex = okami::MapTypes::FromMapId(currentMapId);

            GROUP("Current Map State")
            {
                ImGui::Text("Map: %s", okami::MapTypes::GetName(currentMapIndex));
                mapGroup(currentMapIndex);
            }

            GROUP("All Maps State")
            {
                for (unsigned i = 0; i < okami::MapTypes::NUM_MAP_TYPES; i++)
                {
                    GROUP(okami::MapTypes::GetName(i))
                    {
                        mapGroup(i);
                    }
                }
            }
        }
        catch (...)
        {
            ImGui::Text("Map State: Not accessible");
        }
    }
    ImGui::End();

    WOLF_IMGUI_END();
}

void initializeDevToolsGUI()
{
    // Set up shared ImGui allocators
    if (!wolf::setupSharedImGuiAllocators())
    {
        wolf::logError("DevTools: Failed to setup shared ImGui allocators");
        return;
    }

    // Initialize ImGui backend
    WOLF_IMGUI_INIT_BACKEND();

    // Register GUI window with WOLF
    if (!wolf::registerGuiWindow("DevTools", renderDevToolsWindow, true))
    {
        wolf::logError("DevTools: Failed to register GUI window");
        return;
    }

    // Register cleanup handler
    wolf::registerCleanupHandler(
        []()
        {
            wolf::unregisterGuiWindow("DevTools");
            wolf::logInfo("DevTools: GUI cleanup complete");
        });
}
