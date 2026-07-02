#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"

namespace ArtCade::EditorNative {

// The viewport's pixel rectangle inside the window. RmlUi knows only this rect;
// the renderer never reads project state to obtain it. Pure data, so picking and
// the renderer can share the same transform without depending on Raylib.
struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool valid() const { return width > 0 && height > 0; }
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

// The single source of the viewport's world<->screen mapping. It mirrors the
// Raylib Camera2D the renderer uses (rotation 0): the renderer builds its camera
// from this, and picking inverts the same transform, so a click maps to exactly
// what is drawn.
struct SceneViewCamera {
    Vec2  offset{};      // screen-space point the target maps to (viewport centre)
    Vec2  target{};      // world-space point shown at offset
    float zoom = 1.f;
};

SceneViewCamera makeSceneViewCamera(const ViewportRect& rect,
                                    const EditorSceneViewState& view,
                                    Vec2 worldSize);

// screen = (world - target) * zoom + offset  =>  inverse below.
Vec2 screenToWorld(const SceneViewCamera& camera, Vec2 screen);

} // namespace ArtCade::EditorNative
