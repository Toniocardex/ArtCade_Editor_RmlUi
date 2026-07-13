#include "editor-native/app/editor_coordinator.h"

#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tilemap_stroke_math.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

struct TilemapEditTargetResolution {
    const SceneInstanceDef* instance = nullptr;
    std::string error;

    explicit operator bool() const { return instance != nullptr; }
};

TilemapEditTargetResolution resolveTilemapEditTarget(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId entityId,
    const std::string& activeLayerId,
    const std::optional<TileId>& selectedTileId,
    bool requireSelectedTile) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId, entityId);
    if (!instance || !instance->tilemap.has_value()) {
        return {nullptr, "Selected instance has no Tilemap component"};
    }
    if (document.isInstanceLayerLocked(sceneId, *instance)) {
        return {nullptr, "Layer is locked"};
    }
    if (document.effectiveLayerId(sceneId, *instance) != activeLayerId) {
        return {nullptr, "Selected instance is not on the active layer"};
    }
    const TilesetAsset* tileset =
        document.findTilesetAsset(instance->tilemap->tilesetAssetId);
    if (!tileset) {
        return {nullptr, "Tilemap references a missing tileset"};
    }
    if (requireSelectedTile && !selectedTileId) {
        return {nullptr, "No tile selected"};
    }
    if (requireSelectedTile && selectedTileId) {
        const bool tileExists = std::any_of(
            tileset->tiles.begin(), tileset->tiles.end(),
            [&](const TileDefinition& tile) { return tile.id == *selectedTileId; });
        if (!tileExists) {
            return {nullptr, "Selected tile does not belong to the active tileset"};
        }
    }
    return {instance, {}};
}

} // namespace

EditorOperationResult EditorCoordinator::apply(const BeginTilePaintStrokeIntent& intent) {
    if (isPlaying()) {
        return finishIntent(EditorOperationResult::failure("Cannot paint while Play is running"));
    }
    if (state_.tilemapEditor.pendingStroke || state_.tilemapEditor.pendingRectangle) {
        return finishIntent(EditorOperationResult::failure("Another tilemap operation is already in progress"));
    }
    const TilemapEditTargetResolution target = resolveTilemapEditTarget(
        document_, intent.sceneId, intent.entityId, activeLayerId(intent.sceneId),
        state_.tilemapEditor.selectedTileId, intent.tool == EditorTool::Brush);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    PendingTileStroke stroke;
    stroke.sceneId = intent.sceneId;
    stroke.entityId = intent.entityId;
    stroke.tool = intent.tool;
    stroke.lastCell = intent.cell;
    const TilemapCell before = readTilemapCell(*inst->tilemap, intent.cell);
    const TilemapCell after = (intent.tool == EditorTool::Eraser)
        ? std::nullopt
        : TilemapCell{TilemapCellValue{*state_.tilemapEditor.selectedTileId, TileTransformFlags::None}};
    stroke.changes[packTilemapCellCoord(intent.cell)] = TilemapCellChange{intent.cell, before, after};

    state_.tilemapEditor.pendingStroke = std::move(stroke);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const UpdateTilePaintStrokeIntent& intent) {
    if (!state_.tilemapEditor.pendingStroke) {
        return finishIntent(EditorOperationResult::failure("No paint stroke in progress"));
    }
    PendingTileStroke& stroke = *state_.tilemapEditor.pendingStroke;
    const SceneInstanceDef* inst = document_.findInstanceInScene(stroke.sceneId, stroke.entityId);
    if (!inst || !inst->tilemap.has_value()) {
        state_.tilemapEditor.pendingStroke.reset();
        return finishIntent(EditorOperationResult::failure("Tilemap component no longer exists"));
    }
    const std::vector<TilemapCellCoord> path = stroke.lastCell
        ? rasterizeCellLine(*stroke.lastCell, intent.cell)
        : std::vector<TilemapCellCoord>{intent.cell};
    const TilemapCell after = (stroke.tool == EditorTool::Eraser)
        ? std::nullopt
        : TilemapCell{TilemapCellValue{*state_.tilemapEditor.selectedTileId, TileTransformFlags::None}};
    for (const TilemapCellCoord& cell : path) {
        const std::int64_t key = packTilemapCellCoord(cell);
        auto it = stroke.changes.find(key);
        if (it == stroke.changes.end()) {
            stroke.changes[key] = TilemapCellChange{cell, readTilemapCell(*inst->tilemap, cell), after};
        } else {
            it->second.after = after;   // revisited cell: before stays as first captured
        }
    }
    stroke.lastCell = intent.cell;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const EndTilePaintStrokeIntent&) {
    state_.tilemapEditor.pendingStroke.reset();
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const CancelTilePaintStrokeIntent&) {
    state_.tilemapEditor.pendingStroke.reset();
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SelectPaintTileIntent& intent) {
    state_.tilemapEditor.selectedTileId = intent.tileId;
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

EditorOperationResult EditorCoordinator::apply(const SetHoveredTilemapCellIntent& intent) {
    state_.tilemapEditor.hoveredCell = intent.cell;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetRectangleShapeModeIntent& intent) {
    state_.tilemapEditor.rectangleOutlineMode = intent.outlineOnly;
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

EditorOperationResult EditorCoordinator::apply(const BeginTileRectangleIntent& intent) {
    if (isPlaying()) {
        return finishIntent(EditorOperationResult::failure("Cannot paint while Play is running"));
    }
    if (state_.tilemapEditor.pendingStroke || state_.tilemapEditor.pendingRectangle) {
        return finishIntent(EditorOperationResult::failure("Another tilemap operation is already in progress"));
    }
    const TilemapEditTargetResolution target = resolveTilemapEditTarget(
        document_, intent.sceneId, intent.entityId, activeLayerId(intent.sceneId),
        state_.tilemapEditor.selectedTileId, true);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    PendingTileRectangle rect;
    rect.sceneId = intent.sceneId;
    rect.entityId = intent.entityId;
    rect.replacement = TilemapCellValue{*state_.tilemapEditor.selectedTileId, TileTransformFlags::None};
    rect.outlineOnly = state_.tilemapEditor.rectangleOutlineMode;
    rect.startCell = intent.cell;
    rect.currentCell = intent.cell;
    const TileRegionBuildResult preview = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement);
    // A single-cell box never approaches kMaxTilePaintOperationCells, so
    // `preview.error` cannot be set here; the real check happens on every
    // subsequent Update and again, authoritatively, on Commit.
    rect.previewChanges = preview.changes;

    state_.tilemapEditor.pendingRectangle = std::move(rect);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const UpdateTileRectangleIntent& intent) {
    if (!state_.tilemapEditor.pendingRectangle) {
        return finishIntent(EditorOperationResult::failure("No rectangle operation in progress"));
    }
    PendingTileRectangle& rect = *state_.tilemapEditor.pendingRectangle;
    if (rect.currentCell.cellX == intent.cell.cellX && rect.currentCell.cellY == intent.cell.cellY) {
        return EditorOperationResult::success(EditorInvalidation::None);   // unmoved: skip the recompute
    }
    const SceneInstanceDef* inst = document_.findInstanceInScene(rect.sceneId, rect.entityId);
    if (!inst || !inst->tilemap.has_value()) {
        state_.tilemapEditor.pendingRectangle.reset();
        return finishIntent(EditorOperationResult::failure("Tilemap component no longer exists"));
    }
    rect.currentCell = intent.cell;
    const TileRegionBuildResult preview = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement);
    // Over the limit mid-drag: keep showing the last valid preview rather
    // than blanking it. Commit re-derives the delta fresh and enforces the
    // limit for real, so this can never let an oversized paint through.
    if (!preview.error) rect.previewChanges = preview.changes;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const CommitTileRectangleIntent&) {
    if (!state_.tilemapEditor.pendingRectangle) {
        return finishIntent(EditorOperationResult::failure("No rectangle operation in progress"));
    }
    // Copy, then clear unconditionally: this is the one applicative operation
    // for the whole drag, so every exit path below - success, no-op, missing
    // entity, over-limit - leaves no pendingRectangle behind.
    const PendingTileRectangle rect = *state_.tilemapEditor.pendingRectangle;
    state_.tilemapEditor.pendingRectangle.reset();
    accumulate(EditorInvalidation::Viewport);

    const SceneInstanceDef* inst = document_.findInstanceInScene(rect.sceneId, rect.entityId);
    if (!inst || !inst->tilemap.has_value()) {
        return finishIntent(EditorOperationResult::failure("Tilemap component no longer exists"));
    }
    const TileRegionBuildResult built = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, rect.replacement);
    if (built.error) {
        return finishIntent(EditorOperationResult::failure(*built.error));
    }
    if (built.changes.empty()) {
        return EditorOperationResult::success(EditorInvalidation::Viewport);   // no-op: nothing to commit
    }
    return execute(PaintTilemapCellsCommand{rect.sceneId, rect.entityId, built.changes});
}

EditorOperationResult EditorCoordinator::apply(const CancelTileRectangleIntent&) {
    state_.tilemapEditor.pendingRectangle.reset();
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const FillTilemapIntent& intent) {
    if (isPlaying()) {
        return finishIntent(EditorOperationResult::failure("Cannot paint while Play is running"));
    }
    if (state_.tilemapEditor.pendingStroke || state_.tilemapEditor.pendingRectangle) {
        return finishIntent(EditorOperationResult::failure("Another tilemap operation is already in progress"));
    }
    const TilemapEditTargetResolution target = resolveTilemapEditTarget(
        document_, intent.sceneId, intent.entityId, activeLayerId(intent.sceneId),
        state_.tilemapEditor.selectedTileId, true);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    const TilemapCell replacement =
        TilemapCellValue{*state_.tilemapEditor.selectedTileId, TileTransformFlags::None};
    const TileRegionBuildResult built = floodFillChanges(*inst->tilemap, intent.cell, replacement);
    if (built.error) {
        return finishIntent(EditorOperationResult::failure(*built.error));
    }
    if (built.changes.empty()) {
        return EditorOperationResult::success(EditorInvalidation::None);   // target already this tile
    }
    return execute(PaintTilemapCellsCommand{intent.sceneId, intent.entityId, built.changes});
}

} // namespace ArtCade::EditorNative
