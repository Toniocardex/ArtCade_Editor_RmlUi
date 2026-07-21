#include "editor-native/view/tilemap_paint_overlay.h"

#include "editor-native/model/project_document.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

void drawTilemapPaintOverlay(const ProjectDocument& document, const TilemapEditorState& tilemapEditor,
                             EditorTool effectiveTool,
                             const SceneId& sceneId, EntityId entityId,
                             const SceneViewportProjection& projection) {
    const ViewportRect& rect = projection.visibleRect;
    if (!tilemapEditor.hoveredCell || !rect.valid()) return;
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId, entityId);
    if (!inst || !inst->tilemap.has_value()) return;

    const SceneViewCamera& vc = projection.camera;
    Camera2D cam{};
    cam.offset = Vector2{vc.offset.x, vc.offset.y};
    cam.target = Vector2{vc.target.x, vc.target.y};
    cam.zoom = vc.zoom;
    cam.rotation = 0.f;

    // The Brush stamps its whole N x M footprint anchored at the hovered
    // cell; every other tool (and any stamp of one cell) keeps the single-
    // cell cursor. Rectangle/Fill repeat the pattern from their own gesture
    // anchor, so a footprint cursor would be misleading there.
    int footprintW = 1;
    int footprintH = 1;
    if (effectiveTool == EditorTool::Brush && tilemapEditor.stamp
        && tilemapEditor.stamp->sourceTilesetAssetId == inst->tilemap->tilesetAssetId) {
        footprintW = tilemapEditor.stamp->width;
        footprintH = tilemapEditor.stamp->height;
    }

    const Vec2 cellSize = inst->tilemap->cellSize;
    const TilemapCellCoord cell = *tilemapEditor.hoveredCell;
    const Rectangle box{
        inst->transform.position.x + static_cast<float>(cell.cellX) * cellSize.x,
        inst->transform.position.y + static_cast<float>(cell.cellY) * cellSize.y,
        cellSize.x * static_cast<float>(footprintW),
        cellSize.y * static_cast<float>(footprintH),
    };

    const bool erasing = tilemapEditor.pendingStroke
        && tilemapEditor.pendingStroke->tool == EditorTool::Eraser;
    const Color color = erasing ? Color{230, 90, 120, 230} : Color{96, 165, 250, 230};

    BeginScissorMode(rect.x, rect.y, rect.width, rect.height);
    BeginMode2D(cam);
    DrawRectangleLinesEx(box, 2.f / cam.zoom, color);
    EndMode2D();
    EndScissorMode();
}

} // namespace ArtCade::EditorNative
