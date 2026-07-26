#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

/** Rename an Object Type local variable and rewrite Text/Gauge bindKeys. */
class RenameObjectTypeLocalVariableCommand final : public EditorCommand {
public:
    RenameObjectTypeLocalVariableCommand(ObjectTypeId objectTypeId,
                                         GameVariableId oldKey,
                                         GameVariableId newKey);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameObjectTypeLocalVariable"; }

private:
    ObjectTypeId objectTypeId_;
    GameVariableId oldKey_;
    GameVariableId newKey_;
};

/** Delete an unreferenced Object Type local variable. */
class RemoveObjectTypeLocalVariableCommand final : public EditorCommand {
public:
    RemoveObjectTypeLocalVariableCommand(ObjectTypeId objectTypeId, GameVariableId key);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveObjectTypeLocalVariable"; }

private:
    ObjectTypeId objectTypeId_;
    GameVariableId key_;
    std::optional<GameVariableDefinition> removed_;
    std::size_t index_ = 0;
};

/** Change local variable type when Text/Gauge compatibility allows it. */
class SetObjectTypeLocalVariableTypeCommand final : public EditorCommand {
public:
    SetObjectTypeLocalVariableTypeCommand(ObjectTypeId objectTypeId,
                                          GameVariableId key,
                                          GameVariableDefinition::Type type);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeLocalVariableType"; }

private:
    ObjectTypeId objectTypeId_;
    GameVariableId key_;
    GameVariableDefinition::Type next_;
    std::optional<GameVariableDefinition::Type> previousType_;
    std::optional<GameVariableValue> previousValue_;
};

std::size_t countObjectTypeLocalVariableReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    const GameVariableId& key);

} // namespace ArtCade::EditorNative
