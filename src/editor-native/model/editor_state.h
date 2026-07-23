#pragma once

#include "editor-native/model/script_editor_state.h"

#include "core/types.h"
#include "editor-native/model/selection_state.h"
#include "editor-native/model/tile_stamp.h"
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
constexpr float kCellSize = 32.0f;
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

// Inspector Tile Palette sheet view (zoom 1.0 = max(fit-to-hole,
// min-readable-tile), like the Tileset Editor's canvas whose limits it mirrors).
namespace TilePaletteViewLimits {
constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 16.0f;
} // namespace TilePaletteViewLimits

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
    // Sheet multi-select (SpriteFrameId). Timeline uses occurrence indices.
    std::vector<SpriteFrameId> selectedSheetFrames;
    std::vector<std::size_t> selectedTimelineIndices;
    // Slicing is frame-count driven: the user says how many frames across/down,
    // and the cell size is derived from the sheet's pixel dimensions (which live
    // in the texture, not here). Margin/spacing trim padded atlases. These fields
    // are the live draft until Apply/confirm commits ReplaceAnimationFrames.
    int sliceColumns = 4;
    int sliceRows = 1;
    int sliceMargin = 0;
    int sliceSpacing = 0;
    bool pendingResliceConfirm = false;
    // Replace Source Image: staged target until the user confirms.
    std::optional<AssetId> pendingSourceImageId;
    float sheetZoom = 1.0f;
    Vec2 sheetPan{};
    bool previewPlaying = false;
    float previewElapsed = 0.0f;
    float previewSpeed = 1.0f;   // workspace-only multiplier
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

enum class CenterWorkspaceMode { Scene, Logic, Script };
enum class LogicBoardTab { Rules, GeneratedLua };

struct LogicBoardEditorState {
    std::optional<ObjectTypeId> objectTypeId;
    LogicBoardTab tab = LogicBoardTab::Rules;
    std::string search;
    /** Slice 4 — expanded + highlighted after diagnostic navigation. */
    std::optional<LogicRuleId> focusRuleId;
    std::string highlightBlockTypeId;
    std::string highlightPropertyKey;
};

// The clip the Sprite Animation Editor focuses: the selected clip when it
// belongs to this asset, else the first clip, else none.
inline const SpriteAnimationClipDef* editorSheetClip(
    const SpriteAnimationAssetDef& asset,
    const std::optional<std::string>& selectedClipId) {
    if (selectedClipId) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == *selectedClipId) return &clip;
        }
    }
    return asset.clips.empty() ? nullptr : &asset.clips.front();
}

inline AssetId editorSheetImageId(const SpriteAnimationAssetDef& asset,
                                  const std::optional<std::string>& /*selectedClipId*/) {
    return asset.sourceImageAssetId;
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
    Rectangle,
    Fill,
};

// The single authority for "does this tool paint/edit tilemap cells" - used
// both to gate tilemap input routing and to decide when the active tool must
// fall back to Select because the current selection no longer supports it
// (EditorCoordinator::reconcileTilemapEditingContext). Select and Pan are
// deliberately never tilemap tools.
inline bool isTilemapTool(EditorTool tool) {
    return tool == EditorTool::Brush || tool == EditorTool::Eraser || tool == EditorTool::Picker
        || tool == EditorTool::Rectangle || tool == EditorTool::Fill;
}

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
    // Brush only: the stamp captured once at stroke-begin (mirrors
    // PendingTileRectangle's capture-once doctrine) - reselecting mid-drag
    // never changes the stroke already in progress. Every interpolated brush
    // position anchors one whole N x M footprint (stampPlacementsAt); the
    // Eraser ignores this and stays a single-cell tool.
    TilemapTileStamp stamp;
    std::optional<TilemapCellCoord>                     lastCell;
    std::unordered_map<std::int64_t, TilemapCellChange> changes;
};

// An in-progress Rectangle Solid/Outline drag: pointer-down to pointer-up,
// never touching ProjectDocument until pointer-up dispatches exactly one
// PaintTilemapCellsCommand (built by rectangleFillChanges/
// rectangleOutlineChanges, tilemap_region_math.h). sceneId/entityId/
// replacement/outlineOnly are all captured once at BeginTileRectangleIntent
// and never re-read afterward - so a tile reselected, a Shape toggle flipped,
// or a different entity selected mid-drag has zero effect on this rectangle;
// only the commit path (resolved by sceneId/entityId, never "current
// selection") can ever apply it. previewChanges is a cache updated only when
// currentCell actually moves, not recomputed every render frame.
struct PendingTileRectangle {
    SceneId          sceneId;
    EntityId         entityId = INVALID_ENTITY;
    // Captured once at BeginTileRectangleIntent; the region repeats it as a
    // pattern (stampPatternTileAt) anchored at startCell, holes skipped.
    TilemapTileStamp stamp;
    bool             outlineOnly = false;
    TilemapCellCoord startCell;
    TilemapCellCoord currentCell;
    std::vector<TilemapCellChange> previewChanges;
};

// Per-tileset view of the Tile Palette sheet (workspace-only, never
// persisted, never dirties). Keyed by tileset asset id so two tilemaps
// sharing a tileset share the view, while switching to a different tileset
// never inherits a far-off scroll/zoom meant for another sheet. Stale entries
// (deleted tilesets) are pruned by reconcileTilemapEditingContext.
//
// textureScale is absolute (screen pixels per texture pixel): 1× / 2× / 3×
// are integer steps for pixel art. scrollOffset positions the sheet's
// top-left inside the hole (scrollbar authority is this vector alone).
struct TilePaletteViewState {
    float textureScale = 2.0f;
    Vec2  scrollOffset{};
    bool  initialized = false;
    bool  gridVisible = true;
};

enum class TilePaletteFitKind {
    Sheet = 0,
    Content = 1,
    Selection = 2,
};

// One-shot Fit request consumed by the app once hole + empty-mask are known.
// Never persisted; cleared after apply.
struct TilePalettePendingFit {
    AssetId tilesetAssetId;
    TilePaletteFitKind kind = TilePaletteFitKind::Content;
};

// Workspace state for tilemap painting. Invariants: no ProjectDocument copy;
// no duplicated "active entity" (read from SelectionState at stroke-begin,
// then pinned into PendingTileStroke::entityId for that stroke only);
// pendingStroke never persists past pointer-up/Escape/lost-focus; the stamp
// is workspace-only and carries its source tileset id - tile ids are only
// unique within one tileset, so a stamp must never survive the tilemap
// switching to a different tileset even if the ids happen to collide
// (reconcileStampAgainstTileset). pendingStroke and pendingRectangle are
// mutually exclusive: EditorCoordinator rejects starting either while the
// other is set, and switching EditorTool cancels whichever is pending.
struct TilemapEditorState {
    std::optional<TilemapTileStamp>     stamp;
    std::unordered_map<AssetId, TilePaletteViewState> paletteViews;
    std::optional<TilePalettePendingFit> pendingPaletteFit;
    std::optional<TilemapCellCoord>     hoveredCell;
    std::optional<PendingTileStroke>    pendingStroke;
    std::optional<PendingTileRectangle> pendingRectangle;
    // Shape toggle for the Rectangle tool (Solid/Outline). A persistent UI
    // preference, not tied to any one drag - read once into
    // PendingTileRectangle::outlineOnly when a drag begins.
    bool rectangleOutlineMode = false;
    // Momentary tool override for a gesture in progress (today: Eraser via
    // right-click) - deliberately separate from EditorState::activeTool,
    // which stays the user's own persistent tool selection throughout the
    // gesture. See EditorCoordinator::effectiveTilemapTool().
    std::optional<EditorTool> temporaryToolOverride;
};

// Shared workspace state. This is not saved into the project file.
struct EditorState {
    // Workspace focus only. This is NOT the gameplay start scene.
    SceneId activeSceneId;
    SelectionState selection;
    EditorTool activeTool = EditorTool::Select;
    CenterWorkspaceMode centerWorkspaceMode = CenterWorkspaceMode::Scene;
    std::unordered_map<SceneId, EditorSceneViewState> sceneViews;
    SpriteAnimationEditorState spriteAnimationEditor;
    TilesetEditorState tilesetEditor;
    TilemapEditorState tilemapEditor;
    LogicBoardEditorState logicBoardEditor;
    ScriptEditorState scriptEditor;
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

inline float clampTilePaletteZoom(float v) {
    return std::clamp(v, TilePaletteViewLimits::kZoomMin, TilePaletteViewLimits::kZoomMax);
}

} // namespace ArtCade::EditorNative
