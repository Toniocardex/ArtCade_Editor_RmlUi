#pragma once

#include "core/types.h"
#include "editor-native/model/selection_state.h"
#include "editor-native/model/tilemap_cell_access.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace SceneViewLimits {
constexpr float kZoomMin = 0.1f;
constexpr float kZoomMax = 8.0f;
} // namespace SceneViewLimits

namespace SceneGridDefaults {
constexpr float kCellSize = 48.0f;
} // namespace SceneGridDefaults

// Sheet zoom multiplies the fit-to-canvas base scale, so 1.0 = "fit view".
namespace SpriteSheetViewLimits {
constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 16.0f;
} // namespace SpriteSheetViewLimits

namespace TilesetEditorViewLimits {
constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 16.0f;
} // namespace TilesetEditorViewLimits

/** Per-scene editor camera. Stored by SceneId, not the gameplay camera. */
struct EditorSceneViewState {
    Vec2  pan{};
    float zoom = 1.0f;
    // True once the one-time auto-fit has run for this scene. Lives here (not an
    // app-side registry) so it shares the single sceneViews lifecycle: cleared by
    // replaceProject, pruned with a deleted scene, reset for a fresh scene.
    bool  initialized = false;
    // Layer workspace state (never persisted, never dirties): the layer new
    // entities go into, and the layers hidden in the Edit viewport only.
    std::string                     activeLayerId;
    std::unordered_set<std::string> hiddenLayerIds;
    // Scene View workspace controls. Grid visibility and snap are intentionally
    // independent: hiding the grid never disables snapping.
    bool gridVisible = true;
    bool gridSnapEnabled = false;
    float gridCellSize = SceneGridDefaults::kCellSize;
};

struct SpriteAnimationEditorState {
    std::optional<AssetId> openAssetId;
    std::optional<std::string> selectedClipId;
    std::size_t selectedFrameIndex = 0;
    // Slicing is frame-count driven: the user says how many frames across/down,
    // and the cell size is derived from the sheet's pixel dimensions (which live
    // in the texture, not here). Margin/spacing trim padded atlases.
    int sliceColumns = 4;
    int sliceRows = 1;
    int sliceMargin = 0;
    int sliceSpacing = 0;
    float sheetZoom = 1.0f;
    Vec2 sheetPan{};
    bool previewPlaying = false;
    float previewElapsed = 0.0f;
    std::size_t previewFrameIndex = 0;
};

// Workspace state for the Tileset Editor. pendingSlicing is the live-edited
// config the canvas previews - seeded from the asset's committed slicing on
// open, discarded on close/cancel, and never written to ProjectDocument until
// Apply executes ChangeTilesetSlicingCommand.
struct TilesetEditorState {
    std::optional<AssetId> openAssetId;
    TilesetSlicing pendingSlicing;
    float zoom = 1.0f;
    Vec2 pan{};
    std::optional<std::string> selectedTileId;
};

// The sheet the Sprite Animation Editor shows for an asset: the selected clip's
// own image when the selection names a clip of this asset, else the default
// clip's, else the first clip's, else none (fresh asset with no clips yet).
inline const SpriteAnimationClipDef* editorSheetClip(
    const SpriteAnimationAssetDef& asset,
    const std::optional<std::string>& selectedClipId) {
    if (selectedClipId) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == *selectedClipId) return &clip;
        }
    }
    if (!asset.defaultClipId.empty()) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == asset.defaultClipId) return &clip;
        }
    }
    return asset.clips.empty() ? nullptr : &asset.clips.front();
}

inline AssetId editorSheetImageId(const SpriteAnimationAssetDef& asset,
                                  const std::optional<std::string>& selectedClipId) {
    const SpriteAnimationClipDef* clip = editorSheetClip(asset, selectedClipId);
    return clip ? clip->imageId : AssetId{};
}

// Extends the existing "what does interacting with the Scene View currently
// mean" mode (previously Select/Pan only - Pan itself was already declared
// but unwired to any input routing or UI) rather than a second, parallel
// enum for tilemap painting.
enum class EditorTool {
    Select,
    Pan,
    Brush,
    Eraser,
    Picker,
};

// An in-progress paint/erase stroke: pointer-down to pointer-up, never
// touching ProjectDocument until pointer-up dispatches exactly one
// PaintTilemapCellsCommand. sceneId/entityId are captured once at
// stroke-begin, not re-derived from selection each frame, so a stroke stays
// bound to the entity it started on even if selection changes mid-drag.
// Keyed by packTilemapCellCoord(...) rather than a
// std::unordered_map<TilemapCellCoord,...> to avoid this codebase's first
// std::hash specialization (tilemap_validation.cpp packs chunk coordinates
// the identical way for the identical reason).
struct PendingTileStroke {
    SceneId    sceneId;
    EntityId   entityId = INVALID_ENTITY;
    EditorTool tool = EditorTool::Brush;   // Brush or Eraser only
    std::optional<TilemapCellCoord>                     lastCell;
    std::unordered_map<std::int64_t, TilemapCellChange> changes;
};

// Workspace state for tilemap painting. Invariants: no ProjectDocument copy;
// no duplicated "active entity" (read from SelectionState at stroke-begin,
// then pinned into PendingTileStroke::entityId for that stroke only);
// pendingStroke never persists past pointer-up/Escape/lost-focus;
// selectedTileId is workspace-only (mirrors TilesetEditorState::
// selectedTileId's identical optional<string> shape and "no sentinel for
// empty" policy).
struct TilemapEditorState {
    std::optional<TileId>            selectedTileId;
    std::optional<TilemapCellCoord>  hoveredCell;
    std::optional<PendingTileStroke> pendingStroke;
};

// Shared workspace state. This is not saved into the project file.
struct EditorState {
    // Workspace focus only. This is NOT the gameplay start scene.
    SceneId activeSceneId;
    SelectionState selection;
    EditorTool activeTool = EditorTool::Select;
    std::unordered_map<SceneId, EditorSceneViewState> sceneViews;
    SpriteAnimationEditorState spriteAnimationEditor;
    TilesetEditorState tilesetEditor;
    TilemapEditorState tilemapEditor;
};

inline float clampZoom(float v) {
    return std::clamp(v, SceneViewLimits::kZoomMin, SceneViewLimits::kZoomMax);
}

inline float clampSheetZoom(float v) {
    return std::clamp(v, SpriteSheetViewLimits::kZoomMin, SpriteSheetViewLimits::kZoomMax);
}

inline float clampTilesetEditorZoom(float v) {
    return std::clamp(v, TilesetEditorViewLimits::kZoomMin, TilesetEditorViewLimits::kZoomMax);
}

} // namespace ArtCade::EditorNative
