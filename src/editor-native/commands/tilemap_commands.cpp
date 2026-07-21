#include "editor-native/commands/tilemap_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/tilemap_validation.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kTilemapInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;
// Instance creation/removal changes the Hierarchy tree and the Scene
// Inspector's structural facts too - same set entity_commands.cpp uses.
constexpr EditorInvalidation kStructureInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;

bool validCellSize(Vec2 size) {
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0.f && size.y > 0.f;
}
} // namespace

// ----------------------------------------------------------------------------
// CreateTilemapEntityCommand
// ----------------------------------------------------------------------------
CreateTilemapEntityCommand::CreateTilemapEntityCommand(
    SceneId sceneId, EntityId id, std::string objectTypeId, std::string objectTypeName,
    std::string instanceName, Vec2 position, std::string layerId, AssetId tilesetAssetId)
    : sceneId_(std::move(sceneId)), id_(id),
      objectTypeId_(std::move(objectTypeId)), objectTypeName_(std::move(objectTypeName)),
      instanceName_(std::move(instanceName)), position_(position),
      layerId_(std::move(layerId)), tilesetAssetId_(std::move(tilesetAssetId)) {}

EditorOperationResult CreateTilemapEntityCommand::apply(ProjectDocument& document) {
    if (id_ == 0) {
        return EditorOperationResult::failure("Entity id cannot be zero");
    }
    if (objectTypeId_.empty()) {
        return EditorOperationResult::failure("Object type id cannot be empty");
    }
    if (!std::isfinite(position_.x) || !std::isfinite(position_.y)) {
        return EditorOperationResult::failure("Entity position must be finite");
    }
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    // Validate everything up front so the staged commit below cannot fail
    // partially - same contract as CreateEntityWithDefaultTypeCommand.
    if (document.hasObjectType(objectTypeId_)) {
        return EditorOperationResult::failure("Object type already exists");
    }
    if (document.findInstanceInScene(sceneId_, id_)) {
        return EditorOperationResult::failure("An instance with that id already exists");
    }
    if (!layerId_.empty() && !document.hasLayer(sceneId_, layerId_)) {
        return EditorOperationResult::failure("Target layer does not exist in the scene");
    }
    const TilesetAsset* tileset = document.findTilesetAsset(tilesetAssetId_);
    if (!tileset) {
        return EditorOperationResult::failure("Tileset does not exist");
    }

    TilemapComponent component;
    component.tilesetAssetId = tilesetAssetId_;
    // Cell size mirrors the tileset's slicing so painted tiles render 1:1 by
    // default; the slicing struct defaults keep this positive, but a guard
    // costs nothing against a hand-authored degenerate tileset.
    const Vec2 sliced{static_cast<float>(tileset->slicing.tileWidth),
                      static_cast<float>(tileset->slicing.tileHeight)};
    component.cellSize = validCellSize(sliced) ? sliced : Vec2{32.f, 32.f};
    if (const auto err = validateTilemapComponent(document, component)) {
        return EditorOperationResult::failure(*err);
    }

    // "" means the scene default - resolved here, not left on the instance:
    // the canonical project format requires a real, non-empty layerId.
    const std::string targetLayer = layerId_.empty() ? scene->defaultLayerId : layerId_;
    if (!captured_) {
        if (document.isLayerLocked(sceneId_, targetLayer)) {
            return EditorOperationResult::failure(
                "Cannot create Tilemap entity: target layer is locked");
        }
        captured_ = true;
    }

    EntityDef type;
    type.className = objectTypeId_;   // the catalog key (mirrors load: className == id)
    type.name = objectTypeName_;
    // Same neutral placeholder fill CreateEntityWithDefaultTypeCommand uses -
    // visible on both light and dark scene backgrounds until tiles exist.
    type.sprite.fillColor = Vec3{0.42f, 0.45f, 0.52f};

    SceneInstanceDef instance;
    instance.id                 = id_;
    instance.objectTypeId       = objectTypeId_;
    instance.instanceName       = instanceName_;
    instance.transform.position = position_;
    instance.layerId            = targetLayer;
    instance.tilemap            = std::move(component);

    ProjectDoc staged = document.data();
    auto stagedScene = staged.scenes.find(sceneId_);
    if (stagedScene == staged.scenes.end()) {
        return EditorOperationResult::failure("Failed to stage target scene");
    }
    staged.objectTypes.emplace(objectTypeId_, std::move(type));
    stagedScene->second.instances.push_back(std::move(instance));
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, id_));
}

EditorOperationResult CreateTilemapEntityCommand::undo(ProjectDocument& document) {
    if (!captured_) {
        return EditorOperationResult::failure("Cannot undo: command state was not captured");
    }
    ProjectDoc staged = document.data();
    auto sceneIt = staged.scenes.find(sceneId_);
    if (sceneIt == staged.scenes.end()) {
        return EditorOperationResult::failure("Cannot undo: instance missing");
    }
    auto instanceIt = std::find_if(
        sceneIt->second.instances.begin(), sceneIt->second.instances.end(),
        [&](const SceneInstanceDef& instance) { return instance.id == id_; });
    if (instanceIt == sceneIt->second.instances.end()
        || instanceIt->objectTypeId != objectTypeId_) {
        return EditorOperationResult::failure("Cannot undo: instance missing");
    }
    if (staged.objectTypes.find(objectTypeId_) == staged.objectTypes.end()) {
        return EditorOperationResult::failure("Cannot undo: object type missing");
    }
    for (const auto& [otherSceneId, otherScene] : staged.scenes) {
        for (const SceneInstanceDef& instance : otherScene.instances) {
            const bool isCreatedInstance = otherSceneId == sceneId_ && instance.id == id_;
            if (!isCreatedInstance && instance.objectTypeId == objectTypeId_) {
                return EditorOperationResult::failure(
                    "Cannot undo: object type is still referenced");
            }
        }
    }
    sceneIt->second.instances.erase(instanceIt);
    staged.objectTypes.erase(objectTypeId_);
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityRemoved(sceneId_, id_));
}

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
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot add Tilemap component: layer is locked");
        }
        captured_ = true;
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
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot remove Tilemap component: layer is locked");
        }
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
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change Tilemap tileset: layer is locked");
        }
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
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change Tilemap cell size: layer is locked");
        }
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

PaintTilemapCellsCommand::PaintTilemapCellsCommand(
    SceneId sceneId, EntityId id, std::vector<TilemapCellChange> changes)
    : sceneId_(std::move(sceneId)), id_(id), changes_(std::move(changes)) {}

EditorOperationResult PaintTilemapCellsCommand::apply(ProjectDocument& document) {
    if (changes_.empty()) {
        return EditorOperationResult::failure("Paint stroke has no cell changes");
    }
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || !inst->tilemap.has_value()) {
        return EditorOperationResult::failure("Instance has no Tilemap component");
    }
    {
        std::unordered_set<std::int64_t> seen;
        for (const TilemapCellChange& change : changes_) {
            if (!seen.insert(packTilemapCellCoord(change.cell)).second) {
                return EditorOperationResult::failure("Duplicate cell in paint delta");
            }
        }
    }

    TilemapComponent next = *inst->tilemap;   // hypothetical next state, validated before commit
    for (const TilemapCellChange& change : changes_) {
        // before-mismatch atomic-failure check: something else mutated the
        // document between stroke-start and commit. `next` is a local copy,
        // so a failure here discards it - nothing partial ever lands.
        if (!(readTilemapCell(next, change.cell) == change.before)) {
            return EditorOperationResult::failure(
                "Tilemap changed since this stroke began - paint discarded");
        }
        writeTilemapCell(next, change.cell, change.after);   // lazily creates the chunk if needed
    }
    pruneEmptyChunks(next);   // erasing a chunk's last non-empty cell removes it

    if (const auto err = validateTilemapComponent(document, next)) {
        return EditorOperationResult::failure(*err);
    }
    if (!document.setTilemapComponent(sceneId_, id_, next)) {
        return EditorOperationResult::failure("Failed to paint tilemap cells");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

EditorOperationResult PaintTilemapCellsCommand::undo(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst || !inst->tilemap.has_value()) {
        return EditorOperationResult::failure("Cannot undo paint: Tilemap component is missing");
    }
    TilemapComponent restored = *inst->tilemap;
    for (const TilemapCellChange& change : changes_) {
        // Restoring `before` implicitly recreates any chunk apply() created
        // (writeTilemapCell lazily creates on a non-empty `before` too) and
        // implicitly re-empties any chunk apply() emptied.
        writeTilemapCell(restored, change.cell, change.before);
    }
    pruneEmptyChunks(restored);   // removes chunks apply() created that undo now empties again

    if (!document.setTilemapComponent(sceneId_, id_, restored)) {
        return EditorOperationResult::failure("Failed to undo paint");
    }
    return EditorOperationResult::success(
        kTilemapInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::Tilemap));
}

} // namespace ArtCade::EditorNative
