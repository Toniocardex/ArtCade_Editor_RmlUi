#include "editor-native/commands/object_variable_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/variable_references.h"
#include "project-global-variables-format.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kObjectVariableInvalidation =
    EditorInvalidation::LogicBoard | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;

EditorOperationResult changed() {
    return EditorOperationResult::success(
        kObjectVariableInvalidation, DomainChange::projectChanged());
}

EntityDef* findType(ProjectDoc& doc, const ObjectTypeId& objectTypeId) {
    const auto it = doc.objectTypes.find(objectTypeId);
    return it == doc.objectTypes.end() ? nullptr : &it->second;
}

auto findVariable(EntityDef& type, const GameVariableId& key) {
    return std::find_if(type.localVariables.begin(), type.localVariables.end(),
                        [&](const GameVariableDefinition& def) { return def.key == key; });
}

auto findVariable(const EntityDef& type, const GameVariableId& key) {
    return std::find_if(type.localVariables.begin(), type.localVariables.end(),
                        [&](const GameVariableDefinition& def) { return def.key == key; });
}

SceneInstanceDef* findInstance(ProjectDoc& doc, const SceneId& sceneId, EntityId instanceId) {
    const auto sceneIt = doc.scenes.find(sceneId);
    if (sceneIt == doc.scenes.end()) return nullptr;
    for (SceneInstanceDef& instance : sceneIt->second.instances) {
        if (instance.id == instanceId) return &instance;
    }
    return nullptr;
}

GameVariableValue defaultValue(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Number:  return 0.0;
    case GameVariableDefinition::Type::Boolean: return false;
    case GameVariableDefinition::Type::String:  return std::string{};
    }
    return 0.0;
}

/** Same rules as project variables, named for the Object Type that failed. */
bool validateDefinitions(const ObjectTypeId& objectTypeId, const EntityDef& type,
                         std::string& error) {
    if (ProjectJson::validate_game_variable_definitions(type.localVariables, error)) return true;
    error = "Object type \"" + objectTypeId + "\": " + error;
    return false;
}

void rewriteInstanceOverrides(ProjectDoc& doc,
                              const ObjectTypeId& objectTypeId,
                              const GameVariableId& from,
                              const GameVariableId& to) {
    for (auto& [unused, scene] : doc.scenes) {
        (void)unused;
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.objectTypeId != objectTypeId) continue;
            const auto it = instance.localVariableOverrides.find(from);
            if (it == instance.localVariableOverrides.end()) continue;
            GameVariableValue value = std::move(it->second);
            instance.localVariableOverrides.erase(it);
            instance.localVariableOverrides[to] = std::move(value);
        }
    }
}

/**
 * Overrides are dependent state, so a definition that disappears or changes
 * type takes them with it — but only after capture, or undo would restore the
 * definition into a project that had silently lost every instance value.
 */
std::vector<CapturedInstanceOverride> eraseInstanceOverrides(
    ProjectDoc& doc, const ObjectTypeId& objectTypeId, const GameVariableId& key) {
    std::vector<CapturedInstanceOverride> captured;
    for (auto& [sceneId, scene] : doc.scenes) {
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.objectTypeId != objectTypeId) continue;
            const auto it = instance.localVariableOverrides.find(key);
            if (it == instance.localVariableOverrides.end()) continue;
            captured.push_back({sceneId, instance.id, it->second});
            instance.localVariableOverrides.erase(it);
        }
    }
    return captured;
}

void restoreInstanceOverrides(ProjectDoc& doc,
                              const GameVariableId& key,
                              const std::vector<CapturedInstanceOverride>& captured) {
    for (const CapturedInstanceOverride& entry : captured) {
        if (SceneInstanceDef* instance = findInstance(doc, entry.sceneId, entry.instanceId)) {
            instance->localVariableOverrides[key] = entry.value;
        }
    }
}

} // namespace

std::size_t countObjectVariableReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    const GameVariableId& key) {
    return countVariableReferences(
               document.data(), VariableScope::Object, &objectTypeId, key)
        .total();
}

std::optional<GameVariableValue> resolveObjectVariableValue(
    const EntityDef& objectType,
    const SceneInstanceDef& instance,
    const GameVariableId& key) {
    const auto definition = findVariable(objectType, key);
    if (definition == objectType.localVariables.end()) return std::nullopt;
    const auto override = instance.localVariableOverrides.find(key);
    if (override != instance.localVariableOverrides.end()) return override->second;
    return definition->initialValue;
}

// -- definitions -------------------------------------------------------------

AddObjectVariableCommand::AddObjectVariableCommand(
    ObjectTypeId objectTypeId, GameVariableDefinition definition)
    : objectTypeId_(std::move(objectTypeId)), definition_(std::move(definition)) {}

EditorOperationResult AddObjectVariableCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    if (findVariable(*type, definition_.key) != type->localVariables.end())
        return EditorOperationResult::failure("Object variable key already exists");
    type->localVariables.push_back(definition_);
    std::string error;
    if (!validateDefinitions(objectTypeId_, *type, error))
        return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult AddObjectVariableCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    const auto it = findVariable(*type, definition_.key);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo object variable creation");
    if (countVariableReferences(staged, VariableScope::Object, &objectTypeId_,
                                definition_.key).total() != 0) {
        return EditorOperationResult::failure("Cannot undo: object variable is referenced");
    }
    type->localVariables.erase(it);
    eraseInstanceOverrides(staged, objectTypeId_, definition_.key);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

RemoveObjectVariableCommand::RemoveObjectVariableCommand(
    ObjectTypeId objectTypeId, GameVariableId key)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)) {}

EditorOperationResult RemoveObjectVariableCommand::apply(ProjectDocument& document) {
    const std::size_t references = countObjectVariableReferences(document, objectTypeId_, key_);
    if (references != 0) {
        return EditorOperationResult::failure(
            "Cannot delete object variable \"" + key_ + "\": referenced by "
            + std::to_string(references) + " Logic/Text/Gauge reference(s)");
    }
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Unknown object variable");
    if (!captured_) {
        removed_ = *it;
        index_ = static_cast<std::size_t>(std::distance(type->localVariables.begin(), it));
        captured_ = true;
    }
    type->localVariables.erase(it);
    std::vector<CapturedInstanceOverride> erased =
        eraseInstanceOverrides(staged, objectTypeId_, key_);
    if (overrides_.empty()) overrides_ = std::move(erased);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RemoveObjectVariableCommand::undo(ProjectDocument& document) {
    if (!removed_) return EditorOperationResult::failure("Cannot undo object variable deletion");
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    if (findVariable(*type, key_) != type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo: object variable key already exists");
    const std::size_t index = std::min(index_, type->localVariables.size());
    type->localVariables.insert(
        type->localVariables.begin() + static_cast<std::ptrdiff_t>(index), *removed_);
    restoreInstanceOverrides(staged, key_, overrides_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

RenameObjectVariableCommand::RenameObjectVariableCommand(
    ObjectTypeId objectTypeId, GameVariableId oldKey, GameVariableId newKey)
    : objectTypeId_(std::move(objectTypeId))
    , oldKey_(std::move(oldKey))
    , newKey_(std::move(newKey)) {}

EditorOperationResult RenameObjectVariableCommand::apply(ProjectDocument& document) {
    if (oldKey_ == newKey_) return EditorOperationResult::success(EditorInvalidation::None);
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto oldIt = findVariable(*type, oldKey_);
    if (oldIt == type->localVariables.end())
        return EditorOperationResult::failure("Unknown object variable");
    if (findVariable(*type, newKey_) != type->localVariables.end())
        return EditorOperationResult::failure("Object variable key already exists");
    oldIt->key = newKey_;
    renameVariableReferences(staged, VariableScope::Object, &objectTypeId_, oldKey_, newKey_);
    rewriteInstanceOverrides(staged, objectTypeId_, oldKey_, newKey_);
    std::string error;
    if (!validateDefinitions(objectTypeId_, *type, error))
        return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RenameObjectVariableCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    const auto it = findVariable(*type, newKey_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo object variable rename");
    if (findVariable(*type, oldKey_) != type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo: old key already exists");
    it->key = oldKey_;
    renameVariableReferences(staged, VariableScope::Object, &objectTypeId_, newKey_, oldKey_);
    rewriteInstanceOverrides(staged, objectTypeId_, newKey_, oldKey_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetObjectVariableTypeCommand::SetObjectVariableTypeCommand(
    ObjectTypeId objectTypeId, GameVariableId key, GameVariableDefinition::Type type)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)), next_(type) {}

EditorOperationResult SetObjectVariableTypeCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Unknown object variable");
    if (it->type == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (variableReferencesRequireDifferentType(
            staged, VariableScope::Object, &objectTypeId_, key_, next_)) {
        return EditorOperationResult::failure(
            "Cannot change object variable type: referenced Logic/Text/Gauge "
            "references require the current type");
    }
    if (!captured_) {
        previousType_ = it->type;
        previousValue_ = it->initialValue;
        captured_ = true;
    }
    it->type = next_;
    it->initialValue = defaultValue(next_);
    // Every override was authored against the old type, so none of them can
    // survive it. Converting them would be inventing values the author never
    // wrote.
    std::vector<CapturedInstanceOverride> erased =
        eraseInstanceOverrides(staged, objectTypeId_, key_);
    if (overrides_.empty()) overrides_ = std::move(erased);
    std::string error;
    if (!validateDefinitions(objectTypeId_, *type, error))
        return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetObjectVariableTypeCommand::undo(ProjectDocument& document) {
    if (!previousType_ || !previousValue_)
        return EditorOperationResult::failure("Cannot undo object variable type change");
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo: object variable missing");
    it->type = *previousType_;
    it->initialValue = *previousValue_;
    restoreInstanceOverrides(staged, key_, overrides_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetObjectVariableInitialValueCommand::SetObjectVariableInitialValueCommand(
    ObjectTypeId objectTypeId, GameVariableId key, GameVariableValue value)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)), next_(std::move(value)) {}

EditorOperationResult SetObjectVariableInitialValueCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Unknown object variable");
    if (!ProjectJson::game_variable_value_matches_type(next_, it->type))
        return EditorOperationResult::failure("Value does not match the object variable type");
    if (it->initialValue == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!previous_) previous_ = it->initialValue;
    it->initialValue = next_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetObjectVariableInitialValueCommand::undo(ProjectDocument& document) {
    if (!previous_) return EditorOperationResult::failure("Cannot undo object variable value");
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo: object variable missing");
    it->initialValue = *previous_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetObjectVariableDescriptionCommand::SetObjectVariableDescriptionCommand(
    ObjectTypeId objectTypeId, GameVariableId key, std::string description)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)), next_(std::move(description)) {}

EditorOperationResult SetObjectVariableDescriptionCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Unknown object variable");
    if (it->description == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!previous_) previous_ = it->description;
    it->description = next_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetObjectVariableDescriptionCommand::undo(ProjectDocument& document) {
    if (!previous_)
        return EditorOperationResult::failure("Cannot undo object variable description");
    ProjectDoc staged = document.data();
    EntityDef* type = findType(staged, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Cannot undo: object type missing");
    const auto it = findVariable(*type, key_);
    if (it == type->localVariables.end())
        return EditorOperationResult::failure("Cannot undo: object variable missing");
    it->description = *previous_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

// -- instance overrides ------------------------------------------------------

SetInstanceVariableOverrideCommand::SetInstanceVariableOverrideCommand(
    SceneId sceneId, EntityId instanceId, GameVariableId key, GameVariableValue value)
    : sceneId_(std::move(sceneId))
    , instanceId_(instanceId)
    , key_(std::move(key))
    , next_(std::move(value)) {}

EditorOperationResult SetInstanceVariableOverrideCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, instanceId_);
    if (!instance) return EditorOperationResult::failure("Unknown scene instance");
    const EntityDef* type = findType(staged, instance->objectTypeId);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    // An instance overrides a value; it never introduces a definition.
    const auto definition = findVariable(*type, key_);
    if (definition == type->localVariables.end()) {
        return EditorOperationResult::failure(
            "Object type \"" + instance->objectTypeId + "\" defines no variable \"" + key_ + "\"");
    }
    if (!ProjectJson::game_variable_value_matches_type(next_, definition->type))
        return EditorOperationResult::failure("Value does not match the object variable type");
    const auto existing = instance->localVariableOverrides.find(key_);
    const bool unchanged = existing != instance->localVariableOverrides.end()
                        && existing->second == next_;
    if (unchanged) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        if (existing != instance->localVariableOverrides.end()) previous_ = existing->second;
        captured_ = true;
    }
    instance->localVariableOverrides[key_] = next_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetInstanceVariableOverrideCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo instance override");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, instanceId_);
    if (!instance) return EditorOperationResult::failure("Cannot undo: scene instance missing");
    if (previous_) instance->localVariableOverrides[key_] = *previous_;
    else instance->localVariableOverrides.erase(key_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

ClearInstanceVariableOverrideCommand::ClearInstanceVariableOverrideCommand(
    SceneId sceneId, EntityId instanceId, GameVariableId key)
    : sceneId_(std::move(sceneId)), instanceId_(instanceId), key_(std::move(key)) {}

EditorOperationResult ClearInstanceVariableOverrideCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, instanceId_);
    if (!instance) return EditorOperationResult::failure("Unknown scene instance");
    const auto existing = instance->localVariableOverrides.find(key_);
    if (existing == instance->localVariableOverrides.end())
        return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = existing->second;
        captured_ = true;
    }
    instance->localVariableOverrides.erase(existing);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult ClearInstanceVariableOverrideCommand::undo(ProjectDocument& document) {
    if (!previous_) return EditorOperationResult::failure("Cannot undo instance override reset");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, instanceId_);
    if (!instance) return EditorOperationResult::failure("Cannot undo: scene instance missing");
    instance->localVariableOverrides[key_] = *previous_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

} // namespace ArtCade::EditorNative
