#include "editor-native/commands/global_variable_commands.h"

#include "editor-native/model/project_document.h"
#include "project-global-variables-format.h"
#include "logic-core.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kVariableInvalidation =
    EditorInvalidation::LogicBoard | EditorInvalidation::Inspector;

EditorOperationResult changed() {
    return EditorOperationResult::success(
        kVariableInvalidation, DomainChange::projectChanged());
}

auto findVariable(ProjectDoc& doc, const GameVariableId& key) {
    return std::find_if(doc.globalVariables.begin(), doc.globalVariables.end(),
        [&](const GameVariableDefinition& definition) { return definition.key == key; });
}

auto findVariable(const ProjectDoc& doc, const GameVariableId& key) {
    return std::find_if(doc.globalVariables.begin(), doc.globalVariables.end(),
        [&](const GameVariableDefinition& definition) { return definition.key == key; });
}

bool validateVariables(const ProjectDoc& doc, std::string& error) {
    return ProjectJson::validate_current_global_variables_document(doc.globalVariables, error);
}

GameVariableValue defaultValue(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Number:  return 0.0;
    case GameVariableDefinition::Type::Boolean: return false;
    case GameVariableDefinition::Type::String:  return std::string{};
    }
    return 0.0;
}

template <typename Fn>
void forEachBlock(ProjectDoc& doc, Fn&& fn) {
    for (auto& [unused, type] : doc.objectTypes) {
        (void)unused;
        if (!type.logicBoard) continue;
        for (LogicRuleDef& rule : type.logicBoard->rules) {
            fn(rule.trigger);
            for (LogicConditionClause& clause : rule.conditions) fn(clause.block);
            for (LogicBlockDef& action : rule.actions) fn(action);
        }
    }
}

template <typename Fn>
void forEachBlock(const ProjectDoc& doc, Fn&& fn) {
    for (const auto& [unused, type] : doc.objectTypes) {
        (void)unused;
        if (!type.logicBoard) continue;
        for (const LogicRuleDef& rule : type.logicBoard->rules) {
            fn(rule.trigger);
            for (const LogicConditionClause& clause : rule.conditions) fn(clause.block);
            for (const LogicBlockDef& action : rule.actions) fn(action);
        }
    }
}

std::size_t referencesIn(const ProjectDoc& doc, const GameVariableId& key) {
    std::size_t count = 0;
    forEachBlock(doc, [&](const LogicBlockDef& block) {
        for (const LogicPropertyDef& property : block.properties) {
            if (const auto* ref = std::get_if<LogicVariableReference>(&property.value);
                ref && ref->id == key) {
                ++count;
            }
        }
    });
    return count;
}

void renameReferences(ProjectDoc& doc, const GameVariableId& from, const GameVariableId& to) {
    forEachBlock(doc, [&](LogicBlockDef& block) {
        for (LogicPropertyDef& property : block.properties) {
            if (auto* ref = std::get_if<LogicVariableReference>(&property.value);
                ref && ref->id == from) {
                ref->id = to;
            }
        }
    });
}

bool referencesRequireDifferentType(
    const ProjectDoc& doc, const GameVariableId& key, GameVariableDefinition::Type next) {
    bool mismatch = false;
    forEachBlock(doc, [&](const LogicBlockDef& block) {
        if (mismatch) return;
        const auto required = Logic::requiredVariableType(block.typeId);
        if (!required || *required == next) return;
        for (const LogicPropertyDef& property : block.properties) {
            const auto* ref = std::get_if<LogicVariableReference>(&property.value);
            if (ref && ref->id == key) { mismatch = true; return; }
        }
    });
    return mismatch;
}

bool sameValue(const GameVariableValue& a, const GameVariableValue& b) {
    return a == b;
}

} // namespace

std::size_t countGlobalVariableReferences(
    const ProjectDocument& document, const GameVariableId& key) {
    return referencesIn(document.data(), key);
}

AddGlobalVariableCommand::AddGlobalVariableCommand(GameVariableDefinition definition)
    : definition_(std::move(definition)) {}

EditorOperationResult AddGlobalVariableCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    if (findVariable(staged, definition_.key) != staged.globalVariables.end())
        return EditorOperationResult::failure("Global variable key already exists");
    staged.globalVariables.push_back(definition_);
    std::string error;
    if (!validateVariables(staged, error)) return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult AddGlobalVariableCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, definition_.key);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo global variable creation");
    if (referencesIn(staged, definition_.key) != 0)
        return EditorOperationResult::failure("Cannot undo: global variable is referenced");
    staged.globalVariables.erase(it);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

RemoveGlobalVariableCommand::RemoveGlobalVariableCommand(GameVariableId key)
    : key_(std::move(key)) {}

EditorOperationResult RemoveGlobalVariableCommand::apply(ProjectDocument& document) {
    const std::size_t references = referencesIn(document.data(), key_);
    if (references != 0) {
        return EditorOperationResult::failure(
            "Cannot delete global variable \"" + key_ + "\": referenced by "
            + std::to_string(references) + " Logic block(s)");
    }
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Unknown global variable");
    if (!removed_) {
        removed_ = *it;
        index_ = static_cast<std::size_t>(
            std::distance(staged.globalVariables.begin(), it));
    }
    staged.globalVariables.erase(it);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RemoveGlobalVariableCommand::undo(ProjectDocument& document) {
    if (!removed_) return EditorOperationResult::failure("Cannot undo global variable deletion");
    ProjectDoc staged = document.data();
    if (findVariable(staged, key_) != staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo: global variable key already exists");
    const std::size_t index = std::min(index_, staged.globalVariables.size());
    staged.globalVariables.insert(
        staged.globalVariables.begin() + static_cast<std::ptrdiff_t>(index), *removed_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

RenameGlobalVariableCommand::RenameGlobalVariableCommand(
    GameVariableId oldKey, GameVariableId newKey)
    : oldKey_(std::move(oldKey)), newKey_(std::move(newKey)) {}

EditorOperationResult RenameGlobalVariableCommand::apply(ProjectDocument& document) {
    if (oldKey_ == newKey_) return EditorOperationResult::success(EditorInvalidation::None);
    ProjectDoc staged = document.data();
    const auto oldIt = findVariable(staged, oldKey_);
    if (oldIt == staged.globalVariables.end())
        return EditorOperationResult::failure("Unknown global variable");
    if (findVariable(staged, newKey_) != staged.globalVariables.end())
        return EditorOperationResult::failure("Global variable key already exists");
    oldIt->key = newKey_;
    renameReferences(staged, oldKey_, newKey_);
    std::string error;
    if (!validateVariables(staged, error)) return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult RenameGlobalVariableCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, newKey_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo global variable rename");
    if (findVariable(staged, oldKey_) != staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo: old key already exists");
    it->key = oldKey_;
    renameReferences(staged, newKey_, oldKey_);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetGlobalVariableTypeCommand::SetGlobalVariableTypeCommand(
    GameVariableId key, GameVariableDefinition::Type type)
    : key_(std::move(key)), next_(type) {}

EditorOperationResult SetGlobalVariableTypeCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Unknown global variable");
    if (it->type == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (referencesRequireDifferentType(staged, key_, next_))
        return EditorOperationResult::failure(
            "Cannot change global variable type: referenced Logic blocks require the current type");
    if (!previousType_) {
        previousType_ = it->type;
        previousValue_ = it->initialValue;
    }
    it->type = next_;
    it->initialValue = defaultValue(next_);
    std::string error;
    if (!validateVariables(staged, error)) return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetGlobalVariableTypeCommand::undo(ProjectDocument& document) {
    if (!previousType_ || !previousValue_)
        return EditorOperationResult::failure("Cannot undo global variable type change");
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo: global variable missing");
    it->type = *previousType_;
    it->initialValue = *previousValue_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetGlobalVariableInitialValueCommand::SetGlobalVariableInitialValueCommand(
    GameVariableId key, GameVariableValue value)
    : key_(std::move(key)), next_(std::move(value)) {}

EditorOperationResult SetGlobalVariableInitialValueCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Unknown global variable");
    if (sameValue(it->initialValue, next_))
        return EditorOperationResult::success(EditorInvalidation::None);
    if (!previous_) previous_ = it->initialValue;
    it->initialValue = next_;
    std::string error;
    if (!validateVariables(staged, error)) return EditorOperationResult::failure(error);
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetGlobalVariableInitialValueCommand::undo(ProjectDocument& document) {
    if (!previous_) return EditorOperationResult::failure("Cannot undo global variable value change");
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo: global variable missing");
    it->initialValue = *previous_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

SetGlobalVariableDescriptionCommand::SetGlobalVariableDescriptionCommand(
    GameVariableId key, std::string description)
    : key_(std::move(key)), next_(std::move(description)) {}

EditorOperationResult SetGlobalVariableDescriptionCommand::apply(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Unknown global variable");
    if (it->description == next_)
        return EditorOperationResult::success(EditorInvalidation::None);
    if (!previous_) previous_ = it->description;
    it->description = next_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

EditorOperationResult SetGlobalVariableDescriptionCommand::undo(ProjectDocument& document) {
    if (!previous_) return EditorOperationResult::failure("Cannot undo global variable description");
    ProjectDoc staged = document.data();
    const auto it = findVariable(staged, key_);
    if (it == staged.globalVariables.end())
        return EditorOperationResult::failure("Cannot undo: global variable missing");
    it->description = *previous_;
    document.commitStagedCommand(std::move(staged));
    return changed();
}

} // namespace ArtCade::EditorNative
