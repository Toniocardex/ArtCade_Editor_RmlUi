#include "editor-native/model/tilemap_stroke_preview.h"

#include "editor-native/model/project_document.h"

#include <algorithm>

namespace ArtCade::EditorNative {

void applyPendingTilemapStrokePreview(SceneFrameSnapshot& snapshot,
                                      const ProjectDocument& document,
                                      const TilemapEditorState& tilemapEditor) {
    if (!tilemapEditor.pendingStroke) return;
    const PendingTileStroke& stroke = *tilemapEditor.pendingStroke;

    const auto tilemapIt = std::find_if(snapshot.tilemaps.begin(), snapshot.tilemaps.end(),
        [&](const SceneFrameTilemap& t) { return t.entityId == stroke.entityId; });
    if (tilemapIt == snapshot.tilemaps.end()) return;

    const SceneInstanceDef* inst = document.findInstanceInScene(stroke.sceneId, stroke.entityId);
    if (!inst || !inst->tilemap.has_value()) return;
    const TilesetAsset* tileset = document.findTilesetAsset(inst->tilemap->tilesetAssetId);
    if (!tileset) return;

    for (const auto& [key, change] : stroke.changes) {
        const float destX = inst->transform.position.x
            + static_cast<float>(change.cell.cellX) * inst->tilemap->cellSize.x;
        const float destY = inst->transform.position.y
            + static_cast<float>(change.cell.cellY) * inst->tilemap->cellSize.y;

        // Remove any existing drawn cell at this destination first - covers
        // both the Eraser case and "this Brush cell is being overwritten".
        tilemapIt->cells.erase(
            std::remove_if(tilemapIt->cells.begin(), tilemapIt->cells.end(),
                [&](const SceneFrameTilemapCell& c) {
                    return c.destination.x == destX && c.destination.y == destY;
                }),
            tilemapIt->cells.end());

        if (!change.after.has_value()) continue;   // Eraser preview: leave removed

        const auto tileIt = std::find_if(tileset->tiles.begin(), tileset->tiles.end(),
            [&](const TileDefinition& t) { return t.id == change.after->tileId; });
        if (tileIt == tileset->tiles.end()) continue;   // unknown tile id: skip, defensive

        tilemapIt->cells.push_back(SceneFrameTilemapCell{
            SceneFrameRect{destX, destY, inst->tilemap->cellSize.x, inst->tilemap->cellSize.y},
            SceneFrameRect{static_cast<float>(tileIt->x), static_cast<float>(tileIt->y),
                          static_cast<float>(tileIt->width), static_cast<float>(tileIt->height)},
        });
    }
}

} // namespace ArtCade::EditorNative
