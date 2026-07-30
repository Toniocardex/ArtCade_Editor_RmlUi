#pragma once

#include "editor-native/model/editor_state.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/transform_gizmo_math.h"
#include "editor-native/view/canvas_font.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

class TextureCache;
class EditorFontCache;

// Optional transform gizmo overlay (Edit + Select + instance selection only).
struct TransformGizmoOverlay {
    bool visible = false;
    SceneFrameTransform2D geometry{};
    bool showScaleHandles = false;
    TransformHandle hovered = TransformHandle::None;
    TransformHandle active = TransformHandle::None;
    bool showReadout = false;
    Transform previewTransform{};
    Vec2 unscaledSize{32.f, 32.f};
    Vec2 readoutScreen{};
};

// SceneView draws an immutable scene frame projection into a viewport rect.
// It never reads ProjectDocument or editor panels during draw; GPU resources are
// queried through TextureCache, a derived rendering cache.
class SceneView {
public:
    void render(const SceneFrameSnapshot& frame,
                const EditorSceneViewState& view,
                const SceneGridDefinition& displayGrid,
                const SceneViewportProjection& projection,
                const TextureCache& textures,
                const CanvasFont& canvasFont,
                const EditorFontCache& fonts,
                const TransformGizmoOverlay* gizmo = nullptr) const;
};

} // namespace ArtCade::EditorNative
