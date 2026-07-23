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

// Zoom that frames size entirely inside rect, leaving `padding` pixels clear
// on every edge. Pure math: Fit View (world) and Play Fit (Game View) share it.
// 1.0 when size is degenerate.
float computeFitZoom(Vec2 size, const ViewportRect& rect, float padding);

// ADR-0018: Play camera from Game View + runtime center (never editor pan/zoom).
struct PlayViewportProjectionInput {
    Vec2 worldSize;
    Vec2 gameViewportSize;
    Vec2 cameraCenter;
    ViewportRect hostRect;
    float padding = 0.f;
};

EditorSceneViewState resolvePlayView(const PlayViewportProjectionInput& input);

// screen = (world - target) * zoom + offset  =>  inverse below.
Vec2 screenToWorld(const SceneViewCamera& camera, Vec2 screen);

// Bundles the two rectangles a frame's Scene View work needs, plus the camera
// resolved from them. `visibleRect` is the true, currently visible/clippable
// area - scissor, hit-test, Fit's own scale, and default-spawn centring all
// stay authoritative on it. `cameraAnchorRect` anchors the camera only, and
// may be taller than visibleRect (e.g. while the Tile Palette dock eats space
// from the bottom of the Scene View) so a panel opening/closing crops the
// view instead of recentring it - the content already on screen never visibly
// moves just because a dock toggled, even though nothing in ProjectDocument
// changed. When no dock (or other panel) is narrowing the view, the two rects
// are identical and this degenerates to today's single-rect behaviour.
struct SceneViewportProjection {
    ViewportRect visibleRect;
    ViewportRect cameraAnchorRect;
    SceneViewCamera camera;
};

// The sole place a Scene View camera gets built from live rects. Render,
// pick, drag, paint, zoom-under-cursor and the pointer readout all go through
// this - never makeSceneViewCamera directly - so they can never independently
// diverge from one another the way the pre-fix bug (dock toggle visibly
// translating painted tiles) came from a formula recomputed ad hoc per call
// site.
SceneViewportProjection resolveSceneViewportProjection(const ViewportRect& visibleRect,
                                                        const ViewportRect& cameraAnchorRect,
                                                        const EditorSceneViewState& view,
                                                        Vec2 worldSize);

} // namespace ArtCade::EditorNative
