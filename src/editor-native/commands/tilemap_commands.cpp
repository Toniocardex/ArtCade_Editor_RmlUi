#include "editor-native/commands/tilemap_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/tilemap_validation.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kTilemapInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

bool validCellSize(Vec2 size) {
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0.f && size.y > 0.f;
}
} // namespace

AddTilemapComponentCommand::AddTilemapComponentCommand(
    SceneId sceneId, EntityId id, TilemapComponent component)
    : sceneId_(std::move(sceneId)), id_(id), component_(std::move(component)) {}

EditorOperationResult AddTilemapComponentCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || inst->tilemap) {
        return EditorOperationResult::failure("Cannot add Tilemap component");
    }
    if (const auto err = validateTilemapComponent(document, component_)) {
        return EditorOperationResult::failure(*err);
    }
    if (!document.addTilemapComponent(sceneId_, id_, component_)) {
        return EditorOperationResult::failure("Failed to add Tilemap component");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::Tilemap));
}

EditorOperationResult AddTilemapComponentCommand::undo(ProjectDocument& document) {
    if (!document.removeTilemapComponent(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo Tilemap component add");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::Tilemap));
}

RemoveTilemapComponentCommand::RemoveTilemapComponentCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult RemoveTilemapComponentCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || !inst->tilemap) {
        return EditorOperationResult::failure("Instance has no Tilemap component");
    }
    if (!captured_) {
        removed_ = *inst->tilemap;
        captured_ = true;
    }
    if (!document.removeTilemapComponent(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to remove Tilemap component");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::Tilemap));
}

EditorOperationResult RemoveTilemapComponentCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addTilemapComponent(sceneId_, id_, removed_)) {
        return EditorOperationResult::failure("Cannot undo Tilemap component removal");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::Tilemap));
}

SetTilemapTilesetCommand::SetTilemapTilesetCommand(
    SceneId sceneId, EntityId id, AssetId tilesetAssetId)
    : sceneId_(std::move(sceneId)), id_(id), next_(std::move(tilesetAssetId)) {}

EditorOperationResult SetTilemapTilesetCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || !inst->tilemap) {
        return EditorOperationResult::failure("Tilemap component is missing");
    }
    if (inst->tilemap->tilesetAssetId == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    // Validate a hypothetical copy with the new tileset swapped in, so a
    // change that would strand an already-used tile id (not reachable via
    // any Command yet, but reachable via a hand-authored/migrated document)
    // is rejected up front rather than producing a state only the next
    // full-document validation pass would catch.
    TilemapComponent hypothetical = *inst->tilemap;
    hypothetical.tilesetAssetId = next_;
    if (const auto err = validateTilemapComponent(document, hypothetical)) {
        return EditorOperationResult::failure(*err);
    }
    if (!captured_) {
        previous_ = inst->tilemap->tilesetAssetId;
        captured_ = true;
    }
    if (!document.setTilemapTileset(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set Tilemap tileset");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

EditorOperationResult SetTilemapTilesetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTilemapTileset(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo Tilemap tileset change");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

SetTilemapCellSizeCommand::SetTilemapCellSizeCommand(
    SceneId sceneId, EntityId id, Vec2 cellSize)
    : sceneId_(std::move(sceneId)), id_(id), next_(cellSize) {}

EditorOperationResult SetTilemapCellSizeCommand::apply(ProjectDocument& document) {
    if (!validCellSize(next_)) {
        return EditorOperationResult::failure("Tilemap cell size must be positive");
    }
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || !inst->tilemap) {
        return EditorOperationResult::failure("Tilemap component is missing");
    }
    if (inst->tilemap->cellSize.x == next_.x && inst->tilemap->cellSize.y == next_.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = inst->tilemap->cellSize;
        captured_ = true;
    }
    if (!document.setTilemapCellSize(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set Tilemap cell size");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

EditorOperationResult SetTilemapCellSizeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTilemapCellSize(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo Tilemap cell size change");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

} // namespace ArtCade::EditorNative
