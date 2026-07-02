#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// BoxCollider2D is object-type owned. The selected instance only lets the
// Inspector discover the authoritative objectTypeId before constructing these.
class AddBoxColliderCommand final : public EditorCommand {
public:
    explicit AddBoxColliderCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddBoxCollider"; }

private:
    std::string objectTypeId_;
};

class RemoveBoxColliderCommand final : public EditorCommand {
public:
    explicit RemoveBoxColliderCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveBoxCollider"; }

private:
    std::string objectTypeId_;
    BoxCollider2DComponent removed_{};
    bool captured_ = false;
};

class SetBoxColliderOffsetCommand final : public EditorCommand {
public:
    SetBoxColliderOffsetCommand(std::string objectTypeId, Vec2 offset);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetBoxColliderOffset"; }

private:
    std::string objectTypeId_;
    Vec2 next_{};
    Vec2 previous_{};
    bool captured_ = false;
};

class SetBoxColliderSizeCommand final : public EditorCommand {
public:
    SetBoxColliderSizeCommand(std::string objectTypeId, Vec2 size);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetBoxColliderSize"; }

private:
    std::string objectTypeId_;
    Vec2 next_{};
    Vec2 previous_{};
    bool captured_ = false;
};

class SetBoxColliderEnabledCommand final : public EditorCommand {
public:
    SetBoxColliderEnabledCommand(std::string objectTypeId, bool enabled);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetBoxColliderEnabled"; }

private:
    std::string objectTypeId_;
    bool next_ = true;
    bool previous_ = true;
    bool captured_ = false;
};

class SetBoxColliderModeCommand final : public EditorCommand {
public:
    SetBoxColliderModeCommand(std::string objectTypeId, BoxColliderMode mode);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetBoxColliderMode"; }

private:
    std::string objectTypeId_;
    BoxColliderMode next_ = BoxColliderMode::Solid;
    BoxColliderMode previous_ = BoxColliderMode::Solid;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
