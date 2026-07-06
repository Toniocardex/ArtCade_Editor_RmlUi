#pragma once

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

// Brush/Eraser/Picker input for the Scene View. Mirrors routeViewportPickDrag's
// shape and call convention exactly; called from the same per-frame site in
// editor_app.cpp. routeViewportPickDrag itself is gated to Select-only so
// entity-pick-drag and tile-painting never both claim the same click.
void routeViewportTilemapPaint(EditorCoordinator& coordinator, const ViewportRect& rect,
                               const RmlInputResult& rml);

} // namespace ArtCade::EditorNative
