#include "editor-native/view/tilemap_paint_overlay.h"

#include "editor-native/model/project_document.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

void drawTilemapPaintOverlay(const ProjectDocument& document, const TilemapEditorState& tilemapEditor,
                             const SceneId& sceneId, EntityId entityId,
                             const ViewportRect& rect, const EditorSceneViewState& view, Vec2 worldSize) {
    if (!tilemapEditor.hoveredCell || !rect.valid()) return;
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId, entityId);
    if (!inst || !inst->tilemap.has_value()) return;

    const SceneViewCamera vc = makeSceneViewCamera(rect, view, worldSize);
    Camera2D cam{};
    cam.offset = Vector2{vc.offset.x, vc.offset.y};
    cam.target = Vector2{vc.target.x, vc.target.y};
    cam.zoom = vc.zoom;
    cam.rotation = 0.f;

    const Vec2 cellSize = inst->tilemap->cellSize;
    const TilemapCellCoord cell = *tilemapEditor.hoveredCell;
    const Rectangle box{
        inst->transform.position.x + static_cast<float>(cell.cellX) * cellSize.x,
        inst->transform.position.y + static_cast<float>(cell.cellY) * cellSize.y,
        cellSize.x, cellSize.y,
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
