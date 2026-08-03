#pragma once

#include "core/types.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/view/scene_view_camera.h"

#include <optional>

namespace ArtCade::EditorNative {

class ProjectDocument;

// ---------------------------------------------------------------------------
// Gizmo handles / interaction state (workspace-only; never persisted)
// ---------------------------------------------------------------------------

enum class TransformHandle {
    None,
    Body,
    CornerTL,
    CornerTR,
    CornerBR,
    CornerBL,
    EdgeT,
    EdgeR,
    EdgeB,
    EdgeL,
};

struct TransformInteractionState {
    bool active = false;
    SceneId sceneId;
    EntityId entityId = INVALID_ENTITY;
    TransformHandle handle = TransformHandle::None;
    Transform originalTransform{};
    Transform previewTransform{};
    Vec2 startMouseWorld{};
    Vec2 fixedAnchorWorld{};
    Vec2 unscaledSize{32.f, 32.f};
    // ADR-0057: captured at gesture begin; immutable for the resize.
    Vec2 effectivePivot{0.5f, 0.5f};
};

inline void cancelTransformInteraction(TransformInteractionState& state) {
    state = TransformInteractionState{};
}

inline constexpr float kTransformHandleVisualPx = 8.f;
inline constexpr float kTransformHandleHitPx = 12.f;

/** World-space extent of a visual handle at the given camera zoom. */
inline float transformHandleWorldExtent(float zoom, float screenPx) {
    const float z = zoom != 0.f ? zoom : 1.f;
    return screenPx / z;
}

// ---------------------------------------------------------------------------
// Capabilities + transform geometry (pure queries)
// ---------------------------------------------------------------------------

struct TransformGizmoCapabilities {
    bool canMove = false;
    bool canScale = false;
};

TransformGizmoCapabilities resolveTransformGizmoCapabilities(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId entityId);

struct InstanceTransformGeometry {
    SceneFrameTransform2D transform;
    Vec2 unscaledSize{32.f, 32.f};
    bool supportsScale = false;
    // Visual effective pivot (includes flip) used by resize anchoring.
    Vec2 effectivePivot{0.5f, 0.5f};
};

std::optional<InstanceTransformGeometry> resolveInstanceTransformGeometry(
    const ProjectDocument& document,
    const SceneFrameSnapshot& frame,
    const SceneId& sceneId,
    EntityId entityId);

// ---------------------------------------------------------------------------
// Pure math (no RmlUi / Raylib / Coordinator / document mutation)
// ---------------------------------------------------------------------------

std::optional<Vec2> transformHandleWorldPosition(
    const SceneFrameTransform2D& geometry,
    TransformHandle handle);

TransformHandle hitTestTransformHandle(
    const SceneFrameTransform2D& geometry,
    bool canScale,
    const SceneViewCamera& camera,
    Vec2 screenMouse);

bool transformBodyContainsWorldPoint(
    const SceneFrameTransform2D& geometry,
    Vec2 worldPoint);

Transform clampAuthoringScale(Transform transform);

Transform moveTransformFromPointer(
    const TransformInteractionState& state,
    Vec2 currentMouseWorld);

// Optional grid snap for resize: free edge/corner lengths become integer
// multiples of cellSize measured from the fixed opposite edge (1:1 cells).
struct TransformResizeSnap {
    bool enabled = false;
    Vec2 origin{};
    Vec2 cellSize{32.f, 32.f};
};

/** Resize from the active handle. Shift → preserve original scale ratio. */
Transform resizeTransformFromHandle(
    const TransformInteractionState& state,
    Vec2 currentMouseWorld,
    bool preserveAspect,
    const TransformResizeSnap* snap = nullptr);

/** Begin a move or resize gesture from geometry + document transform. */
TransformInteractionState beginTransformInteraction(
    const SceneId& sceneId,
    EntityId entityId,
    TransformHandle handle,
    const Transform& authored,
    const InstanceTransformGeometry& geometry,
    Vec2 mouseWorld);

AuthoredTransformPatch transformPatchForRelease(
    const TransformInteractionState& state);

} // namespace ArtCade::EditorNative
