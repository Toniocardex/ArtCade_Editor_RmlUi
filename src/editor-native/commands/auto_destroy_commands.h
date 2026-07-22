#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// AutoDestroy is authored on the Object Type. Every materialized instance gets
// a fresh runtime timer; _timeAlive is intentionally never authoring data.
class AddAutoDestroyCommand final : public EditorCommand {
public:
    explicit AddAutoDestroyCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddAutoDestroy"; }

private:
    std::string objectTypeId_;
};

class RemoveAutoDestroyCommand final : public EditorCommand {
public:
    explicit RemoveAutoDestroyCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveAutoDestroy"; }

private:
    std::string objectTypeId_;
    AutoDestroyComponent removed_{};
    bool captured_ = false;
};

class SetAutoDestroyLifespanCommand final : public EditorCommand {
public:
    SetAutoDestroyLifespanCommand(std::string objectTypeId, float lifespan);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetAutoDestroyLifespan"; }

private:
    std::string objectTypeId_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
