#include "watches.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <yaml-cpp/yaml.h>

#include "tracer.h"
#include "wolf_framework.hpp"

namespace wolf_tracer
{

namespace
{

std::mutex g_mu;
std::vector<Watch> g_watches;
std::unordered_map<uintptr_t, uint32_t> g_lastValue; // offset -> last masked val
uintptr_t g_mainBase = 0;
bool g_seeded = false;

uint32_t readWord(uintptr_t offsetFromMain)
{
    if (g_mainBase == 0)
        g_mainBase = wolf::getModuleBase("main.dll");
    if (g_mainBase == 0)
        return 0;
    return *reinterpret_cast<volatile uint32_t *>(g_mainBase + offsetFromMain);
}

} // namespace

void seedDefaultWatches()
{
    if (g_seeded)
        return;
    g_seeded = true;

    addWatch(0xB6B2A0, 0xFFFFFFFF, "AreaLoadFlags (letterbox + scene-mode)");
    addWatch(0xB6B2AC, 0xFFFFFFFF, "GlobalGameState[0]");
    addWatch(0xB6B2B4, 0x01000000, "GlobalGameState bit 71 (controls disabled)");
    addWatch(0xB6B240, 0x0000FFFF, "currentMapID");
    addWatch(0xB66574, 0x00000002, "cutscene-completion-bit-1");
    addWatch(0xB6ACC4, 0xFFFFFFFF, "saveStateFlags");
    addWatch(0xB6B2A4, 0xFFFFFFFF, "auxState_aa4");
}

void addWatch(uintptr_t offsetFromMain, uint32_t mask, std::string label)
{
    std::lock_guard lock(g_mu);
    g_watches.push_back({offsetFromMain, mask, std::move(label)});
    g_lastValue[offsetFromMain] = readWord(offsetFromMain) & mask;
}

bool removeWatch(uintptr_t offsetFromMain)
{
    std::lock_guard lock(g_mu);
    auto it = std::find_if(g_watches.begin(), g_watches.end(), [&](const Watch &w) { return w.offsetFromMain == offsetFromMain; });
    if (it == g_watches.end())
        return false;
    g_watches.erase(it);
    g_lastValue.erase(offsetFromMain);
    return true;
}

const std::vector<Watch> &listWatches()
{
    return g_watches;
}

void clearWatches()
{
    std::lock_guard lock(g_mu);
    g_watches.clear();
    g_lastValue.clear();
    // Reset the seed guard so 'watch clear' followed by 'watch defaults'
    // actually re-adds the curated set.
    g_seeded = false;
}

void tickWatches()
{
    if (!isTracingForCurrentMap())
        return;

    std::lock_guard lock(g_mu);
    for (const auto &w : g_watches)
    {
        uint32_t cur = readWord(w.offsetFromMain) & w.mask;
        uint32_t prev = g_lastValue[w.offsetFromMain];
        if (cur != prev)
        {
            TraceEvent ev{};
            ev.kind = TraceEventKind::WatchChange;
            ev.a = w.offsetFromMain;
            ev.b = w.mask;
            ev.c = prev;
            ev.d = cur;
            ev.label = w.label;
            emitEvent(std::move(ev));
            g_lastValue[w.offsetFromMain] = cur;
        }
    }
}

static std::string toHex(uint64_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

bool saveWatches(const std::string &path)
{
    YAML::Emitter out;
    {
        std::lock_guard lock(g_mu);
        out << YAML::BeginSeq;
        for (const auto &w : g_watches)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "offset" << YAML::Value << toHex(w.offsetFromMain);
            out << YAML::Key << "mask" << YAML::Value << toHex(w.mask);
            out << YAML::Key << "label" << YAML::Value << w.label;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }
    std::ofstream f(path);
    if (!f)
        return false;
    f << out.c_str() << "\n";
    return static_cast<bool>(f);
}

bool loadWatches(const std::string &path)
{
    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path);
    }
    catch (const YAML::Exception &e)
    {
        wolf::logWarning("RuntimeTracer: watch load failed: %s", e.what());
        return false;
    }
    if (!root.IsSequence())
        return false;
    clearWatches();
    for (const auto &node : root)
    {
        std::string offStr = node["offset"].as<std::string>("");
        std::string maskStr = node["mask"].as<std::string>("");
        std::string label = node["label"].as<std::string>("");
        if (offStr.empty() || maskStr.empty())
            continue;
        try
        {
            uintptr_t offset = std::stoull(offStr, nullptr, 0);
            uint32_t mask = static_cast<uint32_t>(std::stoul(maskStr, nullptr, 0));
            addWatch(offset, mask, std::move(label));
        }
        catch (const std::exception &e)
        {
            wolf::logWarning("RuntimeTracer: skipped watch entry '%s' / '%s': %s", offStr.c_str(), maskStr.c_str(), e.what());
        }
    }
    return true;
}

} // namespace wolf_tracer
