#include "console.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "hooks.h"
#include "overlay.h"
#include "trace_event.h"
#include "tracer.h"
#include "watches.h"
#include "wolf_framework.hpp"

namespace wolf_tracer
{

namespace
{

uint32_t parseHex32(const std::string &s)
{
    return static_cast<uint32_t>(std::stoul(s, nullptr, 0));
}

uintptr_t parseHexPtr(const std::string &s)
{
    return static_cast<uintptr_t>(std::stoull(s, nullptr, 0));
}

bool dumpEventsToFile(const std::string &path); // defined below

void traceCommand(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        wolf::consolePrintf("usage: trace start|stop|status|setmap|allmaps|dump");
        return;
    }
    const std::string &sub = args[1];
    if (sub == "start")
    {
        setEnabled(true);
        wolf::consolePrintf("trace: ENABLED");
    }
    else if (sub == "stop")
    {
        setEnabled(false);
        wolf::consolePrintf("trace: DISABLED");
    }
    else if (sub == "status")
    {
        const auto &allow = mapAllowlist();
        wolf::consolePrintf("trace: %s, watches=%zu, allowlist=%s", isEnabled() ? "ON" : "OFF", listWatches().size(),
                            allow.empty() ? "(all maps)" : "(filtered)");
        if (!allow.empty())
        {
            std::string s;
            for (auto id : allow)
            {
                char b[8];
                std::snprintf(b, sizeof(b), "0x%X ", id);
                s += b;
            }
            wolf::consolePrintf("  ids: %s", s.c_str());
        }
        auto hs = getHookInstallStatus();
        wolf::consolePrintf("  hooks: state_bit_set=%s schedule_callback=%s "
                            "schedule_script=%s wait_token=%s script_status=%s frame_advance=%s",
                            hs.stateBitSet ? "OK" : "FAIL", hs.scheduleCb ? "OK" : "FAIL", hs.scheduleScript ? "OK" : "INLINED",
                            hs.waitToken ? "OK" : "INLINED", hs.scriptStatus ? "OK" : "FAIL", hs.frameAdvance ? "OK" : "FAIL");
    }
    else if (sub == "setmap")
    {
        std::vector<uint16_t> ids;
        for (std::size_t i = 2; i < args.size(); ++i)
        {
            try
            {
                ids.push_back(static_cast<uint16_t>(parseHex32(args[i])));
            }
            catch (const std::exception &)
            {
                wolf::consolePrintf("trace: setmap: invalid hex id '%s'", args[i].c_str());
                return;
            }
        }
        setMapAllowlist(std::move(ids));
        wolf::consolePrintf("trace: map allowlist set (%zu ids)", args.size() - 2);
    }
    else if (sub == "allmaps")
    {
        setMapAllowlist({});
        wolf::consolePrintf("trace: map filter cleared");
    }
    else if (sub == "dump")
    {
        if (args.size() < 3)
        {
            wolf::consolePrintf("usage: trace dump <path>");
            return;
        }
        if (dumpEventsToFile(args[2]))
            wolf::consolePrintf("trace: dumped to %s", args[2].c_str());
        else
            wolf::consolePrintf("trace: dump FAILED (path not writable?)");
    }
    else
    {
        wolf::consolePrintf("trace: unknown subcommand '%s'", sub.c_str());
    }
}

void watchCommand(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        wolf::consolePrintf("usage: watch add|remove|list|clear|defaults|save|load ...");
        return;
    }
    const std::string &sub = args[1];
    if (sub == "list")
    {
        for (const auto &w : listWatches())
            wolf::consolePrintf("  +0x%llX  mask=0x%X  %s", (unsigned long long)w.offsetFromMain, w.mask, w.label.c_str());
    }
    else if (sub == "add")
    {
        if (args.size() < 5)
        {
            wolf::consolePrintf("usage: watch add <addr> <mask> <label...>");
            return;
        }
        uintptr_t off;
        uint32_t mask;
        try
        {
            off = parseHexPtr(args[2]);
            mask = parseHex32(args[3]);
        }
        catch (const std::exception &)
        {
            wolf::consolePrintf("watch: add: invalid hex addr '%s' or mask '%s'", args[2].c_str(), args[3].c_str());
            return;
        }
        std::string label;
        for (std::size_t i = 4; i < args.size(); ++i)
        {
            if (i > 4)
                label += " ";
            label += args[i];
        }
        addWatch(off, mask, std::move(label));
        wolf::consolePrintf("watch: added");
    }
    else if (sub == "remove")
    {
        if (args.size() < 3)
        {
            wolf::consolePrintf("usage: watch remove <addr>");
            return;
        }
        uintptr_t off;
        try
        {
            off = parseHexPtr(args[2]);
        }
        catch (const std::exception &)
        {
            wolf::consolePrintf("watch: remove: invalid hex addr '%s'", args[2].c_str());
            return;
        }
        bool ok = removeWatch(off);
        wolf::consolePrintf("watch: %s", ok ? "removed" : "not found");
    }
    else if (sub == "save")
    {
        if (args.size() < 3)
        {
            wolf::consolePrintf("usage: watch save <path>");
            return;
        }
        bool ok = saveWatches(args[2]);
        wolf::consolePrintf("watch: %s", ok ? "saved" : "save FAILED");
    }
    else if (sub == "load")
    {
        if (args.size() < 3)
        {
            wolf::consolePrintf("usage: watch load <path>");
            return;
        }
        bool ok = loadWatches(args[2]);
        wolf::consolePrintf("watch: %s", ok ? "loaded" : "load FAILED");
    }
    else if (sub == "defaults")
    {
        seedDefaultWatches();
        wolf::consolePrintf("watch: seeded %zu default entries (idempotent)", listWatches().size());
    }
    else if (sub == "clear")
    {
        clearWatches();
        wolf::consolePrintf("watch: cleared");
    }
    else
    {
        wolf::consolePrintf("watch: unknown subcommand '%s'", sub.c_str());
    }
}

void overlayCommand(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        wolf::consolePrintf("usage: overlay on|off");
        return;
    }
    if (args[1] == "on")
        setOverlayVisible(true);
    else if (args[1] == "off")
        setOverlayVisible(false);
    else
        wolf::consolePrintf("overlay: unknown '%s'", args[1].c_str());
}

std::string toHex(uint64_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

bool dumpEventsToFile(const std::string &path)
{
    auto events = snapshotEvents();
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "tick_at_dump" << YAML::Value << currentTick();
    out << YAML::Key << "event_count" << YAML::Value << events.size();
    out << YAML::Key << "events" << YAML::Value << YAML::BeginSeq;
    for (const auto &e : events)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "tick" << YAML::Value << e.tick;
        out << YAML::Key << "map" << YAML::Value << toHex(e.mapId);
        out << YAML::Key << "kind" << YAML::Value << kindName(e.kind);
        out << YAML::Key << "a" << YAML::Value << toHex(e.a);
        out << YAML::Key << "b" << YAML::Value << toHex(e.b);
        out << YAML::Key << "c" << YAML::Value << toHex(e.c);
        out << YAML::Key << "d" << YAML::Value << toHex(e.d);
        if (!e.label.empty())
            out << YAML::Key << "label" << YAML::Value << e.label;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    std::ofstream f(path);
    if (!f)
        return false;
    f << out.c_str() << "\n";
    return static_cast<bool>(f);
}

} // namespace

void registerTracerCommands()
{
    wolf::addCommand("trace", traceCommand, "trace <start|stop|status|setmap|allmaps|dump>");
    wolf::addCommand("watch", watchCommand, "watch <add|remove|list|clear|defaults|save|load>");
    wolf::addCommand("overlay", overlayCommand, "overlay <on|off>");
}

} // namespace wolf_tracer
