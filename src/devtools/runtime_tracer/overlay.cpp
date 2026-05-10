#include "overlay.h"

#include <atomic>

#include <imgui.h>

#include "memory_accessors.h"
#include "trace_event.h"
#include "tracer.h"
#include "watches.h"

namespace wolf_tracer
{

namespace
{

std::atomic<bool> g_visible{false};

} // namespace

void setOverlayVisible(bool visible)
{
    g_visible.store(visible);
}

bool isOverlayVisible()
{
    return g_visible.load();
}

void drawTracerOverlay()
{
    if (!g_visible.load())
        return;

    bool open = true;
    if (ImGui::Begin("Runtime Tracer", &open))
    {
        uint16_t mapId = ExteriorMapID.is_bound() ? static_cast<uint16_t>(ExteriorMapID.get() & 0xFFFF) : 0;

        ImGui::Text("Tick: %llu", (unsigned long long)currentTick());
        ImGui::Text("Map:  0x%04X", mapId);
        ImGui::Text("Trace: %s", isEnabled() ? "ON" : "OFF");

        if (ImGui::CollapsingHeader("Watches", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("watches", 3, ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Offset");
                ImGui::TableSetupColumn("Mask");
                ImGui::TableSetupColumn("Label");
                ImGui::TableHeadersRow();
                for (const auto &w : listWatches())
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("+0x%llX", (unsigned long long)w.offsetFromMain);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", w.mask);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(w.label.c_str());
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Recent events", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto events = snapshotEvents();
            const std::size_t N = events.size();
            const std::size_t kMaxShown = 200;
            std::size_t start = N > kMaxShown ? N - kMaxShown : 0;
            ImGui::BeginChild("events", ImVec2(0, 280));
            for (std::size_t i = start; i < N; ++i)
            {
                const auto &e = events[i];
                ImGui::Text("t=%llu m=%04X %s a=%llX b=%llX %s", (unsigned long long)e.tick, e.mapId, kindName(e.kind), (unsigned long long)e.a,
                            (unsigned long long)e.b, e.label.c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();

    // The window's close button flips `open` to false; mirror that into our
    // visibility state so `overlay on` can re-open without inconsistency.
    if (!open)
        g_visible.store(false);
}

} // namespace wolf_tracer
