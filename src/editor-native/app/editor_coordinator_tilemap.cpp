#include "editor-native/app/editor_coordinator.h"

#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tilemap_stroke_math.h"
#include "editor-native/model/tileset_grid_geometry.h"

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

// True when every non-hole slot of the stamp names a tile of `tileset`.
bool stampTilesExistIn(const TilemapTileStamp& stamp, const TilesetAsset& tileset) {
    for (const std::optional<TileId>& id : stamp.tiles) {
        if (!id) continue;
        const bool exists = std::any_of(
            tileset.tiles.begin(), tileset.tiles.end(),
            [&](const TileDefinition& tile) { return tile.id == *id; });
        if (!exists) return false;
    }
    return true;
}

TilemapEditTargetResolution resolveTilemapEditTarget(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId entityId,
    const std::string& activeLayerId,
    const std::optional<TilemapTileStamp>& stamp,
    bool requireStamp) {
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
    if (requireStamp) {
        if (!stamp || !stampIsValid(*stamp)) {
            return {nullptr, "No tile selected"};
        }
        // Provenance gate: tile ids are only unique within one tileset, so a
        // stamp from tileset A must never paint a tilemap that uses tileset B
        // - even when B happens to contain identically-named ids.
        if (stamp->sourceTilesetAssetId != instance->tilemap->tilesetAssetId) {
            return {nullptr, "Selected tiles belong to a different tileset"};
        }
        if (!stampTilesExistIn(*stamp, *tileset)) {
            return {nullptr, "Selected tile does not belong to the active tileset"};
        }
    }
    return {instance, {}};
}

// Folds one whole stamp footprint anchored at `anchor` into a stroke's change
// accumulator: new cells capture their live `before`, revisited cells only
// update `after` (before stays as first captured). Stamp holes touch nothing.
void foldStampFootprint(PendingTileStroke& stroke, const TilemapComponent& tilemap,
                        TilemapCellCoord anchor) {
    for (const TileStampPlacement& placement : stampPlacementsAt(stroke.stamp, anchor)) {
        const TilemapCell after =
            TilemapCellValue{placement.tileId, TileTransformFlags::None};
        const std::int64_t key = packTilemapCellCoord(placement.cell);
        auto it = stroke.changes.find(key);
        if (it == stroke.changes.end()) {
            stroke.changes[key] = TilemapCellChange{
                placement.cell, readTilemapCell(tilemap, placement.cell), after};
        } else {
            it->second.after = after;
        }
    }
}

// The pattern provider for Rectangle/Fill: repeats the stamp anchored at
// `anchor` with Euclidean modulo; holes are Skip (touch nothing), never Erase.
// Captures the stamp by reference: the referenced stamp must outlive the
// returned provider (every caller below consumes it within one expression).
TileReplacementProvider stampPatternProvider(const TilemapTileStamp& stamp,
                                             TilemapCellCoord anchor) {
    return [&stamp, anchor](TilemapCellCoord cell) {
        const std::optional<TileId> id = stampPatternTileAt(stamp, anchor, cell);
        return id ? TileReplacementDecision::paint(*id) : TileReplacementDecision::skip();
    };
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
        state_.tilemapEditor.stamp, intent.tool == EditorTool::Brush);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    PendingTileStroke stroke;
    stroke.sceneId = intent.sceneId;
    stroke.entityId = intent.entityId;
    stroke.tool = intent.tool;
    stroke.lastCell = intent.cell;
    if (intent.tool == EditorTool::Eraser) {
        // The Eraser stays a single-cell tool: no stamp, no footprint.
        const TilemapCell before = readTilemapCell(*inst->tilemap, intent.cell);
        stroke.changes[packTilemapCellCoord(intent.cell)] =
            TilemapCellChange{intent.cell, before, std::nullopt};
    } else {
        // Captured once at Begin: reselecting mid-drag never changes this
        // stroke. Every brush position anchors one whole N x M footprint.
        stroke.stamp = *state_.tilemapEditor.stamp;
        foldStampFootprint(stroke, *inst->tilemap, intent.cell);
    }

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
    if (stroke.tool == EditorTool::Eraser) {
        for (const TilemapCellCoord& cell : path) {
            const std::int64_t key = packTilemapCellCoord(cell);
            auto it = stroke.changes.find(key);
            if (it == stroke.changes.end()) {
                stroke.changes[key] = TilemapCellChange{
                    cell, readTilemapCell(*inst->tilemap, cell), std::nullopt};
            } else {
                it->second.after = std::nullopt;   // revisited cell: before stays as first captured
            }
        }
    } else {
        // Each interpolated position anchors a full footprint, so a fast drag
        // leaves no gaps in the stamped band.
        for (const TilemapCellCoord& cell : path) {
            foldStampFootprint(stroke, *inst->tilemap, cell);
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

EditorOperationResult EditorCoordinator::apply(const SelectPaintStampIntent& intent) {
    if (!stampIsValid(intent.stamp)) {
        return finishIntent(EditorOperationResult::failure("Invalid tile selection"));
    }
    // The palette only ever shows the selected instance's own tileset, so a
    // valid selection must name that tileset and only tiles it contains -
    // enforced here too because tests and screenshot hooks reach this intent
    // directly.
    const SceneInstanceDef* inst = document_.findInstanceInScene(
        state_.activeSceneId, state_.selection.primaryEntity);
    if (!inst || !inst->tilemap.has_value()) {
        return finishIntent(EditorOperationResult::failure("Selected instance has no Tilemap component"));
    }
    if (intent.stamp.sourceTilesetAssetId != inst->tilemap->tilesetAssetId) {
        return finishIntent(EditorOperationResult::failure("Selected tiles belong to a different tileset"));
    }
    const TilesetAsset* tileset = document_.findTilesetAsset(inst->tilemap->tilesetAssetId);
    if (!tileset || !stampTilesExistIn(intent.stamp, *tileset)) {
        return finishIntent(EditorOperationResult::failure("Selected tile does not belong to the active tileset"));
    }
    state_.tilemapEditor.stamp = intent.stamp;
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

EditorOperationResult EditorCoordinator::apply(const SelectPaintTileIntent& intent) {
    // Adapter onto the canonical stamp selection: resolve the selected
    // instance's tileset, attach sheet provenance when the tile is
    // grid-aligned, and delegate. Never a second selection path.
    const SceneInstanceDef* inst = document_.findInstanceInScene(
        state_.activeSceneId, state_.selection.primaryEntity);
    if (!inst || !inst->tilemap.has_value()) {
        return finishIntent(EditorOperationResult::failure("Selected instance has no Tilemap component"));
    }
    const TilesetAsset* tileset = document_.findTilesetAsset(inst->tilemap->tilesetAssetId);
    if (!tileset) {
        return finishIntent(EditorOperationResult::failure("Tilemap references a missing tileset"));
    }
    int column = -1;
    int row = -1;
    for (const TileDefinition& tile : tileset->tiles) {
        if (tile.id != intent.tileId) continue;
        if (const std::optional<TilemapCellCoord> cell =
                tilesetGridCellForTileRectUnbounded(tileset->slicing, tile)) {
            column = cell->cellX;
            row = cell->cellY;
        }
        break;
    }
    return apply(SelectPaintStampIntent{
        makeSingleTileStamp(tileset->assetId, intent.tileId, column, row)});
}

EditorOperationResult EditorCoordinator::apply(const SetTilePaletteZoomIntent& intent) {
    if (!NumericValidation::isFinite(intent.zoom) || intent.zoom <= 0.f) {
        return finishIntent(EditorOperationResult::failure("Tile palette zoom must be positive"));
    }
    if (!document_.findTilesetAsset(intent.tilesetAssetId)) {
        return finishIntent(EditorOperationResult::failure("Tile palette zoom targets a missing tileset"));
    }
    TilePaletteViewState& view = state_.tilemapEditor.paletteViews[intent.tilesetAssetId];
    view.zoom = clampTilePaletteZoom(intent.zoom);
    view.initialized = true;
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

EditorOperationResult EditorCoordinator::apply(const PanTilePaletteIntent& intent) {
    if (!NumericValidation::isFinite(intent.delta)) {
        return finishIntent(EditorOperationResult::failure("Tile palette pan must be finite"));
    }
    if (!document_.findTilesetAsset(intent.tilesetAssetId)) {
        return finishIntent(EditorOperationResult::failure("Tile palette pan targets a missing tileset"));
    }
    TilePaletteViewState& view = state_.tilemapEditor.paletteViews[intent.tilesetAssetId];
    const Vec2 next{view.pan.x + intent.delta.x, view.pan.y + intent.delta.y};
    if (!NumericValidation::isFinite(next)) {
        return finishIntent(EditorOperationResult::failure("Tile palette pan must be finite"));
    }
    view.pan = next;
    view.initialized = true;
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
        state_.tilemapEditor.stamp, true);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    PendingTileRectangle rect;
    rect.sceneId = intent.sceneId;
    rect.entityId = intent.entityId;
    rect.stamp = *state_.tilemapEditor.stamp;
    rect.outlineOnly = state_.tilemapEditor.rectangleOutlineMode;
    rect.startCell = intent.cell;
    rect.currentCell = intent.cell;
    const TileReplacementProvider provider = stampPatternProvider(rect.stamp, rect.startCell);
    const TileRegionBuildResult preview = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider);
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
    const TileReplacementProvider provider = stampPatternProvider(rect.stamp, rect.startCell);
    const TileRegionBuildResult preview = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider);
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
    const TileReplacementProvider provider = stampPatternProvider(rect.stamp, rect.startCell);
    const TileRegionBuildResult built = rect.outlineOnly
        ? rectangleOutlineChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider)
        : rectangleFillChanges(*inst->tilemap, rect.startCell, rect.currentCell, provider);
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
        state_.tilemapEditor.stamp, true);
    if (!target) return finishIntent(EditorOperationResult::failure(target.error));
    const SceneInstanceDef* inst = target.instance;

    const TileRegionBuildResult built = floodFillChanges(
        *inst->tilemap, intent.cell,
        stampPatternProvider(*state_.tilemapEditor.stamp, intent.cell));
    if (built.error) {
        return finishIntent(EditorOperationResult::failure(*built.error));
    }
    if (built.changes.empty()) {
        return EditorOperationResult::success(EditorInvalidation::None);   // target already this pattern
    }
    return execute(PaintTilemapCellsCommand{intent.sceneId, intent.entityId, built.changes});
}

} // namespace ArtCade::EditorNative
