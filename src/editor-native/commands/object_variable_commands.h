#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

/**
 * Object variables (ADR-0031): definitions live on the Object Type and are
 * shared by every instance of it; an instance may only override the initial
 * value of a definition that already exists, never introduce one.
 *
 * Overrides are dependent state rather than references: they never block a
 * delete, they follow a rename, and they are dropped — and restored on undo —
 * when the definition disappears or changes type.
 */

/** One override captured before a definition change erased it. */
struct CapturedInstanceOverride {
    SceneId           sceneId;
    EntityId          instanceId = 0;
    GameVariableValue value;
};

class AddObjectVariableCommand final : public EditorCommand {
public:
    AddObjectVariableCommand(ObjectTypeId objectTypeId, GameVariableDefinition definition);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddObjectVariable"; }

private:
    ObjectTypeId           objectTypeId_;
    GameVariableDefinition definition_;
};

/** Remove an unreferenced definition, taking its instance overrides with it. */
class RemoveObjectVariableCommand final : public EditorCommand {
public:
    RemoveObjectVariableCommand(ObjectTypeId objectTypeId, GameVariableId key);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveObjectVariable"; }

private:
    ObjectTypeId                          objectTypeId_;
    GameVariableId                        key_;
    std::optional<GameVariableDefinition> removed_;
    std::size_t                           index_ = 0;
    std::vector<CapturedInstanceOverride> overrides_;
    bool                                  captured_ = false;
};

/** Rename in place: same definition, new key, every reference and override followed. */
class RenameObjectVariableCommand final : public EditorCommand {
public:
    RenameObjectVariableCommand(ObjectTypeId objectTypeId,
                                GameVariableId oldKey,
                                GameVariableId newKey);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameObjectVariable"; }

private:
    ObjectTypeId   objectTypeId_;
    GameVariableId oldKey_;
    GameVariableId newKey_;
};

/**
 * Change type and reset initialValue to that type's deterministic default.
 * Blocked while a reference requires the current type; incompatible instance
 * overrides are dropped and captured for undo, never silently converted.
 */
class SetObjectVariableTypeCommand final : public EditorCommand {
public:
    SetObjectVariableTypeCommand(ObjectTypeId objectTypeId,
                                 GameVariableId key,
                                 GameVariableDefinition::Type type);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectVariableType"; }

private:
    ObjectTypeId                                objectTypeId_;
    GameVariableId                              key_;
    GameVariableDefinition::Type                next_;
    std::optional<GameVariableDefinition::Type> previousType_;
    std::optional<GameVariableValue>            previousValue_;
    std::vector<CapturedInstanceOverride>       overrides_;
    bool                                        captured_ = false;
};

class SetObjectVariableInitialValueCommand final : public EditorCommand {
public:
    SetObjectVariableInitialValueCommand(ObjectTypeId objectTypeId,
                                         GameVariableId key,
                                         GameVariableValue value);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectVariableInitialValue"; }

private:
    ObjectTypeId                     objectTypeId_;
    GameVariableId                   key_;
    GameVariableValue                next_;
    std::optional<GameVariableValue> previous_;
};

class SetObjectVariableDescriptionCommand final : public EditorCommand {
public:
    SetObjectVariableDescriptionCommand(ObjectTypeId objectTypeId,
                                        GameVariableId key,
                                        std::string description);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectVariableDescription"; }

private:
    ObjectTypeId               objectTypeId_;
    GameVariableId             key_;
    std::string                next_;
    std::optional<std::string> previous_;
};

/** Set one instance's initial value for a definition its Object Type owns. */
class SetInstanceVariableOverrideCommand final : public EditorCommand {
public:
    SetInstanceVariableOverrideCommand(SceneId sceneId,
                                       EntityId instanceId,
                                       GameVariableId key,
                                       GameVariableValue value);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetInstanceVariableOverride"; }

private:
    SceneId                          sceneId_;
    EntityId                         instanceId_;
    GameVariableId                   key_;
    GameVariableValue                next_;
    std::optional<GameVariableValue> previous_;
    bool                             captured_ = false;
};

/** Drop the override so the instance falls back to the Object Type default. */
class ClearInstanceVariableOverrideCommand final : public EditorCommand {
public:
    ClearInstanceVariableOverrideCommand(SceneId sceneId,
                                         EntityId instanceId,
                                         GameVariableId key);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ClearInstanceVariableOverride"; }

private:
    SceneId                          sceneId_;
    EntityId                         instanceId_;
    GameVariableId                   key_;
    std::optional<GameVariableValue> previous_;
    bool                             captured_ = false;
};

/** Read-only reference count used before offering Delete. */
std::size_t countObjectVariableReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    const GameVariableId& key);

/**
 * The authored value an instance starts with: its override when set, else the
 * Object Type default. Derived on read — never a third place to store it.
 */
std::optional<GameVariableValue> resolveObjectVariableValue(
    const EntityDef& objectType,
    const SceneInstanceDef& instance,
    const GameVariableId& key);

} // namespace ArtCade::EditorNative
