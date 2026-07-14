#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

enum class ObjectTypeSpriteSourceKind { None, Image, Animation };

class AddSpriteRendererToObjectTypeCommand final : public EditorCommand {
public:
    explicit AddSpriteRendererToObjectTypeCommand(ObjectTypeId objectTypeId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSpriteRendererToObjectType"; }
private:
    ObjectTypeId objectTypeId_;
};

class RemoveSpriteRendererFromObjectTypeCommand final : public EditorCommand {
public:
    explicit RemoveSpriteRendererFromObjectTypeCommand(ObjectTypeId objectTypeId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSpriteRendererFromObjectType"; }
private:
    struct InstanceState {
        SceneId sceneId;
        EntityId entityId = INVALID_ENTITY;
        std::optional<SpriteRendererOverride> renderer;
        std::optional<SpriteAnimatorOverride> animator;
    };
    ObjectTypeId objectTypeId_;
    SpriteRendererComponent renderer_{};
    std::optional<SpriteAnimatorComponent> animator_;
    std::vector<InstanceState> instanceStates_;
    bool captured_ = false;
};

class SetObjectTypeSpriteSourceCommand final : public EditorCommand {
public:
    SetObjectTypeSpriteSourceCommand(ObjectTypeId objectTypeId,
                                     ObjectTypeSpriteSourceKind kind, AssetId assetId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeSpriteSource"; }
private:
    struct AnimatorOverrideState {
        SceneId sceneId;
        EntityId entityId = INVALID_ENTITY;
        std::optional<SpriteAnimatorOverride> animator;
    };
    ObjectTypeId objectTypeId_;
    ObjectTypeSpriteSourceKind kind_ = ObjectTypeSpriteSourceKind::None;
    AssetId assetId_;
    SpriteRendererComponent previousRenderer_{};
    std::optional<SpriteAnimatorComponent> previousAnimator_;
    std::vector<AnimatorOverrideState> previousAnimatorOverrides_;
    bool captured_ = false;
};

class AddSpriteAnimatorToObjectTypeCommand final : public EditorCommand {
public:
    AddSpriteAnimatorToObjectTypeCommand(ObjectTypeId objectTypeId,
                                         SpriteAnimatorComponent component);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSpriteAnimatorToObjectType"; }
private:
    ObjectTypeId objectTypeId_;
    SpriteAnimatorComponent component_{};
};

class RemoveSpriteAnimatorFromObjectTypeCommand final : public EditorCommand {
public:
    explicit RemoveSpriteAnimatorFromObjectTypeCommand(ObjectTypeId objectTypeId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSpriteAnimatorFromObjectType"; }
private:
    struct InstanceState {
        SceneId sceneId;
        EntityId entityId = INVALID_ENTITY;
        std::optional<SpriteAnimatorOverride> animator;
    };
    ObjectTypeId objectTypeId_;
    SpriteAnimatorComponent removed_{};
    std::vector<InstanceState> instanceStates_;
    bool captured_ = false;
};

class SetObjectTypeInitialClipCommand final : public EditorCommand {
public:
    SetObjectTypeInitialClipCommand(ObjectTypeId objectTypeId, std::string clipId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeInitialClip"; }
private:
    ObjectTypeId objectTypeId_;
    std::string next_;
    std::string previous_;
    bool captured_ = false;
};

class SetObjectTypeAutoPlayCommand final : public EditorCommand {
public:
    SetObjectTypeAutoPlayCommand(ObjectTypeId objectTypeId, bool autoPlay);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeAutoPlay"; }
private:
    ObjectTypeId objectTypeId_;
    bool next_ = true;
    bool previous_ = true;
    bool captured_ = false;
};

class SetObjectTypePlaybackSpeedCommand final : public EditorCommand {
public:
    SetObjectTypePlaybackSpeedCommand(ObjectTypeId objectTypeId, float speed);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypePlaybackSpeed"; }
private:
    ObjectTypeId objectTypeId_;
    float next_ = 1.f;
    float previous_ = 1.f;
    bool captured_ = false;
};

class SetInstanceSpriteOverrideCommand final : public EditorCommand {
public:
    SetInstanceSpriteOverrideCommand(SceneId sceneId, EntityId entityId,
                                     SpriteRendererOverride value);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetInstanceSpriteOverride"; }
private:
    SceneId sceneId_;
    EntityId entityId_ = INVALID_ENTITY;
    SpriteRendererOverride next_{};
    std::optional<SpriteRendererOverride> previous_;
    bool captured_ = false;
};

class SetInstanceAnimatorOverrideCommand final : public EditorCommand {
public:
    SetInstanceAnimatorOverrideCommand(SceneId sceneId, EntityId entityId,
                                       SpriteAnimatorOverride value);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetInstanceAnimatorOverride"; }
private:
    SceneId sceneId_;
    EntityId entityId_ = INVALID_ENTITY;
    SpriteAnimatorOverride next_{};
    std::optional<SpriteAnimatorOverride> previous_;
    bool captured_ = false;
};

class ClearInstanceSpriteOverrideCommand final : public EditorCommand {
public:
    ClearInstanceSpriteOverrideCommand(SceneId sceneId, EntityId entityId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ClearInstanceSpriteOverride"; }
private:
    SceneId sceneId_;
    EntityId entityId_ = INVALID_ENTITY;
    SpriteRendererOverride removed_{};
    bool captured_ = false;
};

class ClearInstanceAnimatorOverrideCommand final : public EditorCommand {
public:
    ClearInstanceAnimatorOverrideCommand(SceneId sceneId, EntityId entityId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ClearInstanceAnimatorOverride"; }
private:
    SceneId sceneId_;
    EntityId entityId_ = INVALID_ENTITY;
    SpriteAnimatorOverride removed_{};
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
