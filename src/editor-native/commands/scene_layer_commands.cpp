#include "editor-native/commands/scene_layer_commands.h"

#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kLayerStruct =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector | EditorInvalidation::Viewport;
constexpr EditorInvalidation kLayerMove =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const SceneLayerDef* findLayer(const SceneDef& scene, const std::string& id) {
    for (const SceneLayerDef& l : scene.layers) if (l.id == id) return &l;
    return nullptr;
}

std::size_t layerIndex(const SceneDef& scene, const std::string& id) {
    for (std::size_t i = 0; i < scene.layers.size(); ++i)
        if (scene.layers[i].id == id) return i;
    return scene.layers.size();
}

// True if another layer (not @p selfId) already uses @p name (case-insensitive).
bool nameTaken(const SceneDef& scene, const std::string& name, const std::string& selfId) {
    const std::string target = lower(name);
    for (const SceneLayerDef& l : scene.layers)
        if (l.id != selfId && lower(l.name) == target) return true;
    return false;
}
} // namespace

// ----------------------------------------------------------------------------
AddSceneLayerCommand::AddSceneLayerCommand(SceneId sceneId, std::string layerId,
                                           std::string name, std::size_t index)
    : sceneId_(std::move(sceneId)), layerId_(std::move(layerId)),
      name_(std::move(name)), index_(index) {}

EditorOperationResult AddSceneLayerCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) return EditorOperationResult::failure("No target scene");
    if (name_.empty()) return EditorOperationResult::failure("Layer name cannot be empty");
    if (document.hasLayer(sceneId_, layerId_))
        return EditorOperationResult::failure("Layer id already exists");
    if (nameTaken(*scene, name_, /*selfId*/ {}))
        return EditorOperationResult::failure("Layer name already exists");
    if (!document.addSceneLayer(sceneId_, layerId_, name_, index_))
        return EditorOperationResult::failure("Failed to add layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult AddSceneLayerCommand::undo(ProjectDocument& document) {
    if (!document.removeSceneLayer(sceneId_, layerId_))
        return EditorOperationResult::failure("Cannot undo add layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

// ----------------------------------------------------------------------------
RenameSceneLayerCommand::RenameSceneLayerCommand(SceneId sceneId, std::string layerId,
                                                 std::string name)
    : sceneId_(std::move(sceneId)), layerId_(std::move(layerId)), newName_(std::move(name)) {}

EditorOperationResult RenameSceneLayerCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) return EditorOperationResult::failure("No target scene");
    const SceneLayerDef* layer = findLayer(*scene, layerId_);
    if (!layer) return EditorOperationResult::failure("No such layer");
    if (newName_.empty()) return EditorOperationResult::failure("Layer name cannot be empty");
    if (layer->name == newName_) return EditorOperationResult::success(EditorInvalidation::None);
    if (nameTaken(*scene, newName_, layerId_))
        return EditorOperationResult::failure("Layer name already exists");
    if (!captured_) { oldName_ = layer->name; captured_ = true; }
    if (!document.renameSceneLayer(sceneId_, layerId_, newName_))
        return EditorOperationResult::failure("Failed to rename layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult RenameSceneLayerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.renameSceneLayer(sceneId_, layerId_, oldName_))
        return EditorOperationResult::failure("Cannot undo rename layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

// ----------------------------------------------------------------------------
MoveSceneLayerCommand::MoveSceneLayerCommand(SceneId sceneId, std::string layerId,
                                             std::size_t index)
    : sceneId_(std::move(sceneId)), layerId_(std::move(layerId)), index_(index) {}

EditorOperationResult MoveSceneLayerCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) return EditorOperationResult::failure("No target scene");
    const std::size_t from = layerIndex(*scene, layerId_);
    if (from == scene->layers.size()) return EditorOperationResult::failure("No such layer");
    std::size_t to = index_ >= scene->layers.size() ? scene->layers.size() - 1 : index_;
    if (to == from) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) { oldIndex_ = from; captured_ = true; }
    if (!document.moveSceneLayer(sceneId_, layerId_, index_))
        return EditorOperationResult::failure("Failed to move layer");
    return EditorOperationResult::success(kLayerMove, DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult MoveSceneLayerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.moveSceneLayer(sceneId_, layerId_, oldIndex_))
        return EditorOperationResult::failure("Cannot undo move layer");
    return EditorOperationResult::success(kLayerMove, DomainChange::sceneChanged(sceneId_));
}

// ----------------------------------------------------------------------------
RemoveSceneLayerCommand::RemoveSceneLayerCommand(SceneId sceneId, std::string layerId)
    : sceneId_(std::move(sceneId)), layerId_(std::move(layerId)) {}

EditorOperationResult RemoveSceneLayerCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) return EditorOperationResult::failure("No target scene");
    if (layerId_ == scene->defaultLayerId)
        return EditorOperationResult::failure("The default layer cannot be removed");
    const std::size_t index = layerIndex(*scene, layerId_);
    if (index == scene->layers.size()) return EditorOperationResult::failure("No such layer");
    for (const SceneInstanceDef& inst : scene->instances) {
        if (inst.layerId == layerId_)
            return EditorOperationResult::failure("Layer has entities; move them first");
    }
    if (!captured_) {
        removedName_ = scene->layers[index].name;
        index_ = index;
        captured_ = true;
    }
    if (!document.removeSceneLayer(sceneId_, layerId_))
        return EditorOperationResult::failure("Failed to remove layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult RemoveSceneLayerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addSceneLayer(sceneId_, layerId_, removedName_, index_))
        return EditorOperationResult::failure("Cannot undo remove layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::sceneChanged(sceneId_));
}

// ----------------------------------------------------------------------------
SetEntityLayerCommand::SetEntityLayerCommand(SceneId sceneId, EntityId id, std::string layerId)
    : sceneId_(std::move(sceneId)), id_(id), newLayerId_(std::move(layerId)) {}

EditorOperationResult SetEntityLayerCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst) return EditorOperationResult::failure("No instance with that id in the target scene");
    if (!document.hasLayer(sceneId_, newLayerId_))
        return EditorOperationResult::failure("Target layer does not exist in the scene");
    if (inst->layerId == newLayerId_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) { oldLayerId_ = inst->layerId; captured_ = true; }
    if (!document.setInstanceLayer(sceneId_, id_, newLayerId_))
        return EditorOperationResult::failure("Failed to set entity layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::entityChanged(sceneId_, id_));
}

EditorOperationResult SetEntityLayerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setInstanceLayer(sceneId_, id_, oldLayerId_))
        return EditorOperationResult::failure("Cannot undo set entity layer");
    return EditorOperationResult::success(kLayerStruct, DomainChange::entityChanged(sceneId_, id_));
}

} // namespace ArtCade::EditorNative
