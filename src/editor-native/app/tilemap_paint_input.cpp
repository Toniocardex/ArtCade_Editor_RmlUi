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

void routeViewportTilemapPaint(EditorCoordinator& coordinator,
                               const SceneViewportProjection& projection,
                               const RmlInputResult& rml) {
    const ViewportRect& rect = projection.visibleRect;
    const EditorTool tool = coordinator.effectiveTilemapTool();

    // Lost window focus mid-operation: cancel, never commit - safer to cancel
    // an incomplete stroke/rectangle than commit it. Escape itself is handled
    // by the global router (editor_app.cpp's routeGlobalEscape) via the same
    // shared cancelPendingTilemapGesture(), so this module only owns its own
    // focus-loss trigger, not Escape-key arbitration.
    if ((coordinator.state().tilemapEditor.pendingStroke
         || coordinator.state().tilemapEditor.pendingRectangle)
        && !IsWindowFocused()) {
        coordinator.cancelPendingTilemapGesture();
        return;
    }
    if (!isTilemapTool(tool)) return;

    const ViewportInputContext ctx{rect.contains(GetMouseX(), GetMouseY()),
                                   /*rmlConsumedEvent*/ false, rml.textFocus,
                                   /*rmlPopupOpen*/ false};
    if (!shouldViewportReceiveInput(ctx)) {
        coordinator.cancelPendingTilemapGesture();
        return;
    }

    const SceneId active = coordinator.state().activeSceneId;
    const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
        active, coordinator.selection().primaryEntity);
    if (!inst || !inst->tilemap.has_value()) return;

    const SceneViewCamera& cam = projection.camera;
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
    const Vec2 world = screenToWorld(cam, mouse);
    const TilemapCellCoord cell =
        worldToCell(world, inst->transform.position, inst->tilemap->cellSize);

    // Hover-cell resolution: every frame the paint tool is active and the
    // viewport is eligible for input, regardless of mouse-button state.
    coordinator.apply(SetHoveredTilemapCellIntent{cell});

    // Right-click is a momentary Eraser override, regardless of which paint
    // tool is currently selected (Brush, Eraser, Picker, Rectangle, Fill) -
    // checked first, ahead of every tool-specific branch below, so it always
    // wins the gesture. It never touches EditorState::activeTool, so
    // whatever the user had selected is exactly what's active again the
    // instant the gesture ends - no re-click needed. The Tool row still
    // highlights Eraser during the gesture because it reads
    // coordinator.effectiveTilemapTool(), which layers the override over the
    // persistent tool; once set, `tool` itself becomes Eraser for the rest of
    // this call and every later frame of the gesture, so it naturally falls
    // through the Picker/Fill/Rectangle checks below into the Brush/Eraser
    // section, which already drives Update/End off either mouse button. A
    // failed Begin (locked layer, a Rectangle drag already pending, Play
    // started mid-frame, etc.) leaves no pendingStroke to later trigger the
    // override's own cleanup, so it is cleared right here too - every exit
    // from this gesture ends the override, with no path that can leave it
    // stuck, and a right-click that couldn't start a stroke is just silently
    // ignored rather than corrupting whatever else was in progress.
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        coordinator.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        const EditorOperationResult began =
            coordinator.apply(BeginTilePaintStrokeIntent{active, inst->id, EditorTool::Eraser, cell});
        if (!began.ok) coordinator.apply(EndTemporaryToolOverrideIntent{});
        return;
    }

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

    // Brush / Eraser - the persistent selection, or an Eraser override in
    // progress from the right-click branch above (both funnel through here
    // once `tool` reflects Eraser).
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
        coordinator.apply(EndTemporaryToolOverrideIntent{});   // no-op for a left-click stroke
    }
}

} // namespace ArtCade::EditorNative
