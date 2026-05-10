#pragma once

namespace wolf_tracer
{

// Draw the Runtime Tracer ImGui window. Issues only ImGui:: calls; must be
// invoked from inside an existing WOLF_IMGUI_BEGIN/END frame. The wolf
// rendering rules require ONE wolf-registered GUI window per mod with ONE
// WOLF_IMGUI_BEGIN/END pair per frame; this function therefore piggybacks
// on devtools_gui's frame rather than registering a separate wolf window
// (which would cause a second context to be lazily created without a DX11
// backend bound to it, crashing on first render).
void drawTracerOverlay();

// Toggle / query overlay visibility. The `overlay on|off` console command
// drives these; drawTracerOverlay is a no-op when the overlay is hidden.
void setOverlayVisible(bool visible);
bool isOverlayVisible();

} // namespace wolf_tracer
