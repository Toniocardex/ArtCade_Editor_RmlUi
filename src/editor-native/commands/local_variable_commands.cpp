#include "editor-native/commands/local_variable_commands.h"

#include "editor-native/model/presentation_variable_refs.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kLocalVariableInvalidation =
    EditorInvalidation::LogicBoard | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;

EditorOperationResult changed() {
    return EditorOperationResult::success(
        kLocalVariableInvalidation, DomainChange::projectChanged());
}

auto findLocal(EntityDef& type, const GameVariableId& key) {
    return std::find_if(type.localVariables.begin(), type.localVariables.end(),
                        [&](const GameVariableDefinition& def) { return def.key == key; });
}

auto findLocal(const EntityDef& type, const GameVariableId& key) {
    return std::find_if(type.localVariables.begin(), type.localVariables.end(),
                        [&](const GameVariableDefinition& def) { return def.key == key; });
}

GameVariableValue defaultValue(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Number: return 0.0;
    case GameVariableDefinition::Type::Boolean: return false;
    case GameVariableDefinition::Type::String: return std::string{};
    }
    return 0.0;
}

void rewriteInstanceOverrides(ProjectDoc& doc,
                              const ObjectTypeId& objectTypeId,
                              const GameVariableId& from,
                              const GameVariableId& to) {
    for (auto& [_, scene] : doc.scenes) {
        (void)_;
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

void eraseInstanceOverrides(ProjectDoc& doc,
                            const ObjectTypeId& objectTypeId,
                            const GameVariableId& key) {
    for (auto& [_, scene] : doc.scenes) {
        (void)_;
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.objectTypeId != objectTypeId) continue;
            instance.localVariableOverrides.erase(key);
        }
    }
}

} // namespace

std::size_t countObjectTypeLocalVariableReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    const GameVariableId& key) {
    return countPresentationVariableReferences(
               document.data(), TextBindingScope::Local, &objectTypeId, key)
        .total();
}

RenameObjectTypeLocalVariableCommand::RenameObjectTypeLocalVariableCommand(
    ObjectTypeId objectTypeId, GameVariableId oldKey, GameVariableId newKey)
    : objectTypeId_(std::move(objectTypeId))
    , oldKey_(std::move(oldKey))
    , newKey_(std::move(newKey)) {}

EditorOperationResult RenameObjectTypeLocalVariableCommand::apply(ProjectDocument& document) {
    if (oldKey_ == newKey_) return EditorOperationResult::success(EditorInvalidation::None);
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Unknown object type");
    auto oldIt = findLocal(typeIt->second, oldKey_);
    if (oldIt == typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Unknown local variable");
    if (findLocal(typeIt->second, newKey_) != typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Local variable key already exists");
    oldIt->key = newKey_;
    renamePresentationVariableReferences(
        staged, TextBindingScope::Local, &objectTypeId_, oldKey_, newKey_);
    rewriteInstanceOverrides(staged, objectTypeId_, oldKey_, newKey_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RenameObjectTypeLocalVariableCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Cannot undo local variable rename");
    auto it = findLocal(typeIt->second, newKey_);
    if (it == typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Cannot undo: renamed key missing");
    if (findLocal(typeIt->second, oldKey_) != typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Cannot undo: old key already exists");
    it->key = oldKey_;
    renamePresentationVariableReferences(
        staged, TextBindingScope::Local, &objectTypeId_, newKey_, oldKey_);
    rewriteInstanceOverrides(staged, objectTypeId_, newKey_, oldKey_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

RemoveObjectTypeLocalVariableCommand::RemoveObjectTypeLocalVariableCommand(
    ObjectTypeId objectTypeId, GameVariableId key)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)) {}

EditorOperationResult RemoveObjectTypeLocalVariableCommand::apply(ProjectDocument& document) {
    const std::size_t references = countObjectTypeLocalVariableReferences(
        document, objectTypeId_, key_);
    if (references != 0) {
        return EditorOperationResult::failure(
            "Cannot delete local variable \"" + key_ + "\": referenced by "
            + std::to_string(references) + " Text/Gauge binding(s)");
    }
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Unknown object type");
    auto it = findLocal(typeIt->second, key_);
    if (it == typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Unknown local variable");
    if (!removed_) {
        removed_ = *it;
        index_ = static_cast<std::size_t>(
            std::distance(typeIt->second.localVariables.begin(), it));
    }
    typeIt->second.localVariables.erase(it);
    eraseInstanceOverrides(staged, objectTypeId_, key_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RemoveObjectTypeLocalVariableCommand::undo(ProjectDocument& document) {
    if (!removed_) return EditorOperationResult::failure("Cannot undo local variable deletion");
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Cannot undo: object type missing");
    if (findLocal(typeIt->second, key_) != typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Cannot undo: local variable key already exists");
    const std::size_t index = std::min(index_, typeIt->second.localVariables.size());
    typeIt->second.localVariables.insert(
        typeIt->second.localVariables.begin() + static_cast<std::ptrdiff_t>(index), *removed_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetObjectTypeLocalVariableTypeCommand::SetObjectTypeLocalVariableTypeCommand(
    ObjectTypeId objectTypeId, GameVariableId key, GameVariableDefinition::Type type)
    : objectTypeId_(std::move(objectTypeId)), key_(std::move(key)), next_(type) {}

EditorOperationResult SetObjectTypeLocalVariableTypeCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Unknown object type");
    auto it = findLocal(typeIt->second, key_);
    if (it == typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Unknown local variable");
    if (it->type == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (presentationReferencesRequireDifferentType(
            staged, TextBindingScope::Local, &objectTypeId_, key_, next_)) {
        return EditorOperationResult::failure(
            "Cannot change local variable type: Text/Gauge bindings require the current type");
    }
    if (!previousType_) {
        previousType_ = it->type;
        previousValue_ = it->initialValue;
    }
    it->type = next_;
    it->initialValue = defaultValue(next_);
    eraseInstanceOverrides(staged, objectTypeId_, key_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetObjectTypeLocalVariableTypeCommand::undo(ProjectDocument& document) {
    if (!previousType_ || !previousValue_)
        return EditorOperationResult::failure("Cannot undo local variable type change");
    ProjectDoc staged = document.data();
    const auto typeIt = staged.objectTypes.find(objectTypeId_);
    if (typeIt == staged.objectTypes.end())
        return EditorOperationResult::failure("Cannot undo: object type missing");
    auto it = findLocal(typeIt->second, key_);
    if (it == typeIt->second.localVariables.end())
        return EditorOperationResult::failure("Cannot undo: local variable missing");
    it->type = *previousType_;
    it->initialValue = *previousValue_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

} // namespace ArtCade::EditorNative
