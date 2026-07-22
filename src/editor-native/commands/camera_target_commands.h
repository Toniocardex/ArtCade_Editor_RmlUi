#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

namespace ArtCade::EditorNative {

// CameraTarget is an instance-owned, scene-scoped component (ADR-0003). The
// commands intentionally carry both SceneId and EntityId: no Object Type can
// become an implicit second camera authority.
class AddCameraTargetCommand final : public EditorCommand {
public:
    AddCameraTargetCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddCameraTarget"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
};

class RemoveCameraTargetCommand final : public EditorCommand {
public:
    RemoveCameraTargetCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveCameraTarget"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    CameraTargetComponent removed_{};
    bool captured_ = false;
};

class SetCameraTargetOffsetCommand final : public EditorCommand {
public:
    SetCameraTargetOffsetCommand(SceneId sceneId, EntityId id, Vec2 offset);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetCameraTargetOffset"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    Vec2 next_{};
    CameraTargetComponent previous_{};
    bool captured_ = false;
};

class SetCameraTargetFollowSpeedCommand final : public EditorCommand {
public:
    SetCameraTargetFollowSpeedCommand(SceneId sceneId, EntityId id, float followSpeed);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetCameraTargetFollowSpeed"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    float next_ = 0.f;
    CameraTargetComponent previous_{};
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
