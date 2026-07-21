#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

/** Add one project-global variable. One definition, one undo entry. */
class AddGlobalVariableCommand final : public EditorCommand {
public:
    explicit AddGlobalVariableCommand(GameVariableDefinition definition);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddGlobalVariable"; }
private:
    GameVariableDefinition definition_;
};

/** Remove an unreferenced project-global variable. Referenced variables are blocked. */
class RemoveGlobalVariableCommand final : public EditorCommand {
public:
    explicit RemoveGlobalVariableCommand(GameVariableId key);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveGlobalVariable"; }
private:
    GameVariableId key_;
    std::optional<GameVariableDefinition> removed_;
    std::size_t index_ = 0;
};

/** Rename the stable key and every LogicVariableReference atomically. */
class RenameGlobalVariableCommand final : public EditorCommand {
public:
    RenameGlobalVariableCommand(GameVariableId oldKey, GameVariableId newKey);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameGlobalVariable"; }
private:
    GameVariableId oldKey_;
    GameVariableId newKey_;
};

/** Change type and reset initialValue to that type's deterministic default. */
class SetGlobalVariableTypeCommand final : public EditorCommand {
public:
    SetGlobalVariableTypeCommand(GameVariableId key, GameVariableDefinition::Type type);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetGlobalVariableType"; }
private:
    GameVariableId key_;
    GameVariableDefinition::Type next_;
    std::optional<GameVariableDefinition::Type> previousType_;
    std::optional<GameVariableValue> previousValue_;
};

class SetGlobalVariableInitialValueCommand final : public EditorCommand {
public:
    SetGlobalVariableInitialValueCommand(GameVariableId key, GameVariableValue value);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetGlobalVariableInitialValue"; }
private:
    GameVariableId key_;
    GameVariableValue next_;
    std::optional<GameVariableValue> previous_;
};

class SetGlobalVariableDescriptionCommand final : public EditorCommand {
public:
    SetGlobalVariableDescriptionCommand(GameVariableId key, std::string description);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetGlobalVariableDescription"; }
private:
    GameVariableId key_;
    std::string next_;
    std::optional<std::string> previous_;
};

/** Read-only reference count used by the drawer before offering Delete. */
std::size_t countGlobalVariableReferences(
    const ProjectDocument& document, const GameVariableId& key);

} // namespace ArtCade::EditorNative
