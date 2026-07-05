#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

// =============================================================================
// EditorIntent — changes only the workspace/editor state, never the project,
// never the undo stack (prompt §4). Plain value structs; the coordinator has a
// typed apply() overload for each.
// =============================================================================

struct SelectEntityIntent {
    EntityId entityId = INVALID_ENTITY;
};

struct SelectSceneIntent {
    SceneId sceneId;
};

struct SetViewportZoomIntent {
    SceneId sceneId;
    float   zoom = 1.0f;
};

struct PanViewportIntent {
    SceneId sceneId;
    Vec2    delta;
};

struct SetSceneGridVisibilityIntent {
    SceneId sceneId;
    bool    visible = true;
};

struct SetSceneGridSnapEnabledIntent {
    SceneId sceneId;
    bool    enabled = false;
};

struct SetSceneGridCellSizeIntent {
    SceneId sceneId;
    float   cellSize = SceneGridDefaults::kCellSize;
};

struct OpenSpriteAnimationEditorIntent {
    AssetId assetId;
};

struct CloseSpriteAnimationEditorIntent {};

struct SelectAnimationClipIntent {
    AssetId assetId;
    std::string clipId;
};

// Frame-count driven slicing (workspace). Columns/rows are how many frames the
// sheet is cut into; the cell size is derived from the sheet dimensions at the
// point of use (renderer/slice), so this stays texture-free. Margin/spacing trim
// the sheet edges and inter-cell gaps.
struct SetAnimationSliceGridIntent {
    int columns = 4;
    int rows = 1;
    int margin = 0;
    int spacing = 0;
};

// Sprite sheet canvas navigation (workspace). Zoom is a multiplier over the
// fit-to-canvas base scale; pan is in canvas pixels.
struct SetSpriteSheetZoomIntent {
    float zoom = 1.0f;
};

struct PanSpriteSheetIntent {
    Vec2 delta;
};

// Play/Pause of the clip preview (workspace; never the PlaySession).
struct SetAnimationPreviewPlayingIntent {
    bool playing = false;
};

// Scrub the preview to an exact frame (timeline chip click). Pauses playback so
// the scrubbed frame holds; the index is clamped to the clip's last frame.
struct SetAnimationPreviewFrameIntent {
    std::size_t frameIndex = 0;
};

// Step the paused preview one frame back/forward, wrapping at both ends
// (transport buttons). Pauses playback like a scrub.
struct StepAnimationPreviewIntent {
    int delta = 1;
};

struct SetHierarchyFilterIntent {
    std::string filter;
};

// The layer new entities go into (workspace, per scene). Validated against the
// scene's layers; an invalid id is ignored.
struct SetActiveLayerIntent {
    SceneId     sceneId;
    std::string layerId;
};

// Hide/show a layer in the Edit viewport only (workspace; never the runtime).
struct ToggleLayerEditorVisibilityIntent {
    SceneId     sceneId;
    std::string layerId;
};

struct SetActiveToolIntent {
    EditorTool tool = EditorTool::Select;
};

struct ToggleConsoleIntent {};

struct ResizePanelIntent {
    enum class Panel { Left, Right, Console };
    Panel panel = Panel::Left;
    float size  = 0.0f;
};

} // namespace ArtCade::EditorNative
