#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"

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

struct SetAnimationSlicingIntent {
    int frameWidth = 32;
    int frameHeight = 32;
    int margin = 0;
    int spacing = 0;
};

struct SetAnimationSliceGridIntent {
    int columns = 1;
    int rows = 1;
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
