#include "editor-native/app/tilemap_paint_input.h"

#include "editor-native/app/input_routing.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/model/tilemap_stroke_math.h"

#include <raylib.h>

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

bool isPaintTool(EditorTool tool) {
    return tool == EditorTool::Brush || tool == EditorTool::Eraser || tool == EditorTool::Picker
        || tool == EditorTool::Rectangle || tool == EditorTool::Fill;
}

// screen -> world (existing screenToWorld/makeSceneViewCamera) -> cell, the
// inverse of tilemapRenderCells'/applyPendingTilemapStrokePreview's own
// originPosition.x + cellX*cellSize.x.
TilemapCellCoord worldToCell(Vec2 world, Vec2 originPosition, Vec2 cellSize) {
    return TilemapCellCoord{
        static_cast<int>(std::floor((world.x - originPosition.x) / cellSize.x)),
        static_cast<int>(std::floor((world.y - originPosition.y) / cellSize.y)),
    };
}

} // namespace

void routeViewportTilemapPaint(EditorCoordinator& coordinator, const ViewportRect& rect,
                               const RmlInputResult& rml) {
    const EditorTool tool = coordinator.state().activeTool;

    // Escape / lost window focus mid-operation: cancel, never commit - safer
    // to cancel an incomplete stroke/rectangle than commit it. pendingStroke
    // and pendingRectangle are mutually exclusive, so at most one of these
    // two calls ever does anything.
    if ((coordinator.state().tilemapEditor.pendingStroke
         || coordinator.state().tilemapEditor.pendingRectangle)
        && (IsKeyPressed(KEY_ESCAPE) || !IsWindowFocused())) {
        if (coordinator.state().tilemapEditor.pendingStroke) coordinator.apply(CancelTilePaintStrokeIntent{});
        if (coordinator.state().tilemapEditor.pendingRectangle) coordinator.apply(CancelTileRectangleIntent{});
        return;
    }
    if (!isPaintTool(tool)) return;

    const ViewportInputContext ctx{rect.contains(GetMouseX(), GetMouseY()),
                                   /*rmlConsumedEvent*/ false, rml.textFocus,
                                   /*rmlPopupOpen*/ false};
    if (!shouldViewportReceiveInput(ctx)) {
        if (coordinator.state().tilemapEditor.pendingStroke) coordinator.apply(CancelTilePaintStrokeIntent{});
        if (coordinator.state().tilemapEditor.pendingRectangle) coordinator.apply(CancelTileRectangleIntent{});
        return;
    }

    const SceneId active = coordinator.state().activeSceneId;
    const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
        active, coordinator.selection().primaryEntity);
    if (!inst || !inst->tilemap.has_value()) return;

    const SceneFrameSnapshot frame = collectSceneFrameSnapshot(
        coordinator.document(), active, coordinator.selection().primaryEntity,
        coordinator.sceneView(active).hiddenLayerIds);
    const SceneViewCamera cam =
        makeSceneViewCamera(rect, coordinator.sceneView(active), frame.worldSize);
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
    const Vec2 world = screenToWorld(cam, mouse);
    const TilemapCellCoord cell =
        worldToCell(world, inst->transform.position, inst->tilemap->cellSize);

    // Hover-cell resolution: every frame the paint tool is active and the
    // viewport is eligible for input, regardless of mouse-button state.
    coordinator.apply(SetHoveredTilemapCellIntent{cell});

    if (tool == EditorTool::Picker) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const TilemapCell picked = readTilemapCell(*inst->tilemap, cell);
            if (picked.has_value()) {
                coordinator.apply(SelectPaintTileIntent{picked->tileId});
            }
            // Empty cell: no selection change (recommended policy) - the
            // hovered-cell/"Empty cell" status text is driven separately by
            // hoveredCell + readTilemapCell in the Inspector refresh.
        }
        return;
    }

    if (tool == EditorTool::Fill) {
        // A single click, not a drag: the coordinator builds the delta and
        // dispatches (at most) one Command synchronously - no pending state.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            coordinator.apply(FillTilemapIntent{active, inst->id, cell});
        }
        return;
    }

    if (tool == EditorTool::Rectangle) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            coordinator.apply(BeginTileRectangleIntent{active, inst->id, cell});
            return;
        }
        if (coordinator.state().tilemapEditor.pendingRectangle && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            coordinator.apply(UpdateTileRectangleIntent{cell});
            return;
        }
        if (coordinator.state().tilemapEditor.pendingRectangle
            && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            // The coordinator builds the delta and executes the Command
            // itself (resolved by the pending rectangle's own captured
            // sceneId/entityId, never "current selection") - the router only
            // reports the gesture.
            coordinator.apply(CommitTileRectangleIntent{});
        }
        return;
    }

    // Brush / Eraser. Right-click is a shortcut for Eraser: switching the
    // active tool first (the same SetActiveToolIntent a real click on the
    // Eraser button sends, so it lights up too) means the Begin/Update/End
    // calls below are the exact same call as a left-click Eraser stroke -
    // just also polling the right button, nothing duplicated.
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        coordinator.apply(SetActiveToolIntent{EditorTool::Eraser});
        coordinator.apply(BeginTilePaintStrokeIntent{active, inst->id, EditorTool::Eraser, cell});
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        coordinator.apply(BeginTilePaintStrokeIntent{active, inst->id, tool, cell});
        return;
    }
    if (coordinator.state().tilemapEditor.pendingStroke
        && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT))) {
        coordinator.apply(UpdateTilePaintStrokeIntent{cell});
        return;
    }
    if (coordinator.state().tilemapEditor.pendingStroke
        && (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))) {
        // Pointer-up: normalize (drop before==after), dispatch exactly one
        // Command if non-empty, then clear the preview - in this exact
        // order, reading the finalized accumulator before clearing it.
        const PendingTileStroke& stroke = *coordinator.state().tilemapEditor.pendingStroke;
        std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
        if (!changes.empty()) {
            coordinator.execute(
                PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId, std::move(changes)});
        }
        coordinator.apply(EndTilePaintStrokeIntent{});
    }
}

} // namespace ArtCade::EditorNative
