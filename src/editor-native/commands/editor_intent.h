#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/model/editor_ui_state.h"

#include <cstddef>
#include <optional>
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

struct SetCenterWorkspaceModeIntent {
    CenterWorkspaceMode mode = CenterWorkspaceMode::Scene;
};

struct SelectLogicObjectTypeIntent {
    ObjectTypeId objectTypeId;
};

struct SetLogicBoardTabIntent {
    LogicBoardTab tab = LogicBoardTab::Rules;
};

struct SetLogicBoardSearchIntent {
    std::string search;
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

struct OpenTilesetEditorIntent {
    AssetId assetId;
};

struct CloseTilesetEditorIntent {};

// Pixel-size-first slicing config, live-edited in the workspace and previewed
// on the canvas; never touches ProjectDocument until Apply executes
// ChangeTilesetSlicingCommand (unlike the Sprite Animation Editor's slicing,
// which commits per-field - see spec §7.3 requiring Cancel to leave no
// dirty/revision behind).
struct SetPendingTilesetSlicingIntent {
    TilesetSlicing slicing;
};

// Tileset canvas navigation (workspace). Mirrors SetSpriteSheetZoomIntent/
// PanSpriteSheetIntent exactly.
struct SetTilesetEditorZoomIntent {
    float zoom = 1.0f;
};

struct PanTilesetEditorIntent {
    Vec2 delta;
};

// Hover/click highlight only (workspace) - metadata display on the selected
// tile is a later slice's job.
struct SelectTilesetTileIntent {
    std::string tileId;
};

struct SetHierarchyFilterIntent {
    std::string filter;
};

struct SetConsoleFilterIntent {
    std::string filter;
};

// Level visibility toggles (workspace). Three explicit intents, mirroring
// SetSceneGridVisibilityIntent/SetSceneGridSnapEnabledIntent, rather than one
// intent parameterized by level — editor_intent.h must not depend back on
// ConsoleMessage::Level (defined in editor_coordinator.h, which includes this
// header first).
struct SetConsoleShowInfoIntent {
    bool visible = true;
};
struct SetConsoleShowWarningIntent {
    bool visible = true;
};
struct SetConsoleShowErrorIntent {
    bool visible = true;
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

// Momentary tool override for a gesture in progress - never touches
// EditorState::activeTool (the user's persistent selection). Today's only
// user is the tilemap Eraser-via-right-click shortcut, but the pair is
// generic so a future temporary-tool gesture can reuse it instead of
// duplicating this. Always safe to send End even with no override set (a
// harmless no-op), so every termination path of the gesture it was started
// for - commit, cancel, or a failed Begin - can call it unconditionally.
struct BeginTemporaryToolOverrideIntent {
    EditorTool tool = EditorTool::Eraser;
};
struct EndTemporaryToolOverrideIntent {};

// Begins a paint/erase stroke at pointer-down: validates the target and
// seeds PendingTileStroke with the first cell's change. Rejected (no
// pendingStroke created) if the target has no TilemapComponent, its layer
// is locked, Play is running, its tileset is missing, or (Brush only) no
// tile is selected.
struct BeginTilePaintStrokeIntent {
    SceneId          sceneId;
    EntityId         entityId = INVALID_ENTITY;
    EditorTool       tool = EditorTool::Brush;   // Brush or Eraser
    TilemapCellCoord cell;
};

// One pointer-move sample: resolves the new cell, interpolates from
// pendingStroke's lastCell via rasterizeCellLine, and folds every crossed
// cell into pendingStroke's accumulator. A no-op if no stroke is in
// progress.
struct UpdateTilePaintStrokeIntent {
    TilemapCellCoord cell;
};

// Pointer-up: clears pendingStroke. Does NOT itself dispatch
// PaintTilemapCellsCommand - the input router reads the finalized
// pendingStroke, builds and executes the Command from it, THEN applies this
// intent to clear the preview.
struct EndTilePaintStrokeIntent {};

// Escape / lost window focus / lost pointer capture mid-stroke: discard the
// pending stroke, no Command, no dirty, no history entry. Functionally
// identical to EndTilePaintStrokeIntent today; kept as a separate name for
// call-site legibility.
struct CancelTilePaintStrokeIntent {};

// Picker: workspace-only tile selection. Produces no Undo, dirty, revision
// bump, or tilemap mutation.
struct SelectPaintTileIntent {
    TileId tileId;
};

// Hover highlight + "Empty cell" status text, updated every frame a paint
// tool is active and the viewport is eligible for input.
struct SetHoveredTilemapCellIntent {
    std::optional<TilemapCellCoord> cell;
};

// Rectangle Shape toggle (Solid/Outline) - a persistent workspace preference,
// not itself a drag. Read once into PendingTileRectangle::outlineOnly when a
// new Rectangle drag begins; flipping it mid-drag never changes the drag
// already in progress.
struct SetRectangleShapeModeIntent {
    bool outlineOnly = false;
};

// Begins a Rectangle Solid/Outline drag at pointer-down. Mirrors
// BeginTilePaintStrokeIntent's shape: the coordinator resolves the tile to
// paint from TilemapEditorState::selectedTileId and the shape from
// TilemapEditorState::rectangleOutlineMode itself (not carried on the
// intent), so both are captured exactly once, at this call, into
// PendingTileRectangle. Rejected - no pendingRectangle created - under the
// same conditions as BeginTilePaintStrokeIntent, plus: a pendingStroke or
// another pendingRectangle already in progress (pendingStroke XOR
// pendingRectangle is an invariant).
struct BeginTileRectangleIntent {
    SceneId          sceneId;
    EntityId         entityId = INVALID_ENTITY;
    TilemapCellCoord cell;
};

// One pointer-move sample: moves PendingTileRectangle::currentCell and
// recomputes previewChanges - but only when the cell actually changed, so an
// unmoving mouse does not re-run the rectangle/outline math every frame.
struct UpdateTileRectangleIntent {
    TilemapCellCoord cell;
};

// Pointer-up: the single applicative operation for the whole drag. Rebuilds
// the final delta from pendingRectangle's captured sceneId/entityId/
// replacement/outlineOnly/startCell/currentCell (never "current selection",
// never the live selectedTileId/rectangleOutlineMode), dispatches exactly one
// PaintTilemapCellsCommand if the delta is non-empty and within
// kMaxTilePaintOperationCells, then clears pendingRectangle unconditionally -
// on success, on a too-large delta, on a vanished entity, and on an empty
// no-op delta alike.
struct CommitTileRectangleIntent {};

// Escape / lost window focus mid-drag: discard pendingRectangle, no Command,
// no dirty, no history entry.
struct CancelTileRectangleIntent {};

// One click of the Fill tool: floods from `cell` (resolved to a
// TilemapCellCoord by the router the same way paint/rectangle input is) using
// the tile currently selected in TilemapEditorState::selectedTileId. Not a
// drag - dispatches at most one PaintTilemapCellsCommand synchronously, no
// pending workspace state, no preview cache.
struct FillTilemapIntent {
    SceneId          sceneId;
    EntityId         entityId = INVALID_ENTITY;
    TilemapCellCoord cell;
};

struct ToggleConsoleIntent {};

// Workspace/UI navigation: scroll the Inspector to a property on an entity.
// No document mutation, undo, or dirty flag.
struct RevealInspectorPropertyIntent {
    EntityId          entityId = INVALID_ENTITY;
    InspectorProperty property = InspectorProperty::TilemapCellSize;
};

struct ResizePanelIntent {
    enum class Panel { Left, Right, Console };
    Panel panel = Panel::Left;
    float size  = 0.0f;
};

} // namespace ArtCade::EditorNative
