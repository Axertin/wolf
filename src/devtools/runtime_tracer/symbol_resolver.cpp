#include "symbol_resolver.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

#include <okami/maptype.hpp>

#include "wolf_framework.hpp"

namespace wolf_tracer
{

namespace
{

constexpr uintptr_t kStageHandlerTableOffset = 0x7AD9B0;
constexpr std::size_t kStageEntryStride = 0x30;
constexpr std::size_t kMaxStageEntries = 1024; // defensive cap

// Layout of one DAT_1807ad9b0 entry per spec:
//   +0x00  short  MapID
//   +0x02  short  flags
//   +0x08  ptr    INIT
//   +0x18  ptr    TICK
struct StageEntry
{
    int16_t mapId;
    int16_t flags;
    uint32_t pad04;
    uintptr_t initFn;
    uintptr_t opt1;
    uintptr_t tickFn;
    uintptr_t opt2;
    uintptr_t opt3;
};
static_assert(sizeof(StageEntry) == 0x30, "stage entry stride mismatch");

std::mutex g_mu;
std::unordered_map<uintptr_t, std::string> g_labels;
uintptr_t g_mainBase = 0;
bool g_walked = false;

std::string fallbackLabel(uintptr_t address)
{
    char buf[64];
    if (g_mainBase != 0 && address >= g_mainBase)
    {
        std::snprintf(buf, sizeof(buf), "main.dll+0x%llX", static_cast<unsigned long long>(address - g_mainBase));
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(address));
    }
    return buf;
}

void walkStageHandlerTable()
{
    if (g_walked)
        return;
    if (g_mainBase == 0)
        return;

    auto *entries = reinterpret_cast<const StageEntry *>(g_mainBase + kStageHandlerTableOffset);

    std::lock_guard lock(g_mu);
    for (std::size_t i = 0; i < kMaxStageEntries; ++i)
    {
        const auto &e = entries[i];
        if (static_cast<uint16_t>(e.mapId) == 0xFFFF)
        {
            wolf::logInfo("RuntimeTracer: walked %zu stage entries", i);
            g_walked = true;
            return;
        }

        unsigned mapIdx = okami::MapTypes::FromMapId(static_cast<unsigned>(e.mapId));
        const char *mapName = (mapIdx < okami::MapTypes::NUM_MAP_TYPES) ? okami::MapTypes::GetName(mapIdx) : "?";

        if (e.initFn != 0)
        {
            char label[96];
            std::snprintf(label, sizeof(label), "%s INIT (main.dll+0x%llX)", mapName, static_cast<unsigned long long>(e.initFn - g_mainBase));
            g_labels.emplace(e.initFn, label);
        }
        if (e.tickFn != 0)
        {
            char label[96];
            std::snprintf(label, sizeof(label), "%s TICK (main.dll+0x%llX)", mapName, static_cast<unsigned long long>(e.tickFn - g_mainBase));
            g_labels.emplace(e.tickFn, label);
        }
    }

    wolf::logWarning("RuntimeTracer: stage table walk hit %zu cap without sentinel", kMaxStageEntries);
    g_walked = true;
}

} // namespace

void initializeSymbolResolver()
{
    g_mainBase = wolf::getModuleBase("main.dll");
    if (g_mainBase == 0)
    {
        wolf::logError("RuntimeTracer: cannot resolve main.dll base");
        return;
    }
    walkStageHandlerTable();
}

void addLabel(uintptr_t address, std::string label)
{
    std::lock_guard lock(g_mu);
    g_labels[address] = std::move(label);
}

std::string resolveLabel(uintptr_t address)
{
    {
        std::lock_guard lock(g_mu);
        auto it = g_labels.find(address);
        if (it != g_labels.end())
            return it->second;
    }
    return fallbackLabel(address);
}

} // namespace wolf_tracer
