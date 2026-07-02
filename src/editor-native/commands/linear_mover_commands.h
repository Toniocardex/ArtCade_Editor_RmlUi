#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// LinearMover is object-type owned (canonical gameplay component). The selected
// instance only lets the Inspector discover the authoritative objectTypeId before
// constructing these. Pause is a runtime flag and is not authored here.
class AddLinearMoverCommand final : public EditorCommand {
public:
    explicit AddLinearMoverCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddLinearMover"; }

private:
    std::string objectTypeId_;
};

class RemoveLinearMoverCommand final : public EditorCommand {
public:
    explicit RemoveLinearMoverCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveLinearMover"; }

private:
    std::string objectTypeId_;
    LinearMoverComponent removed_{};
    bool captured_ = false;
};

class SetLinearMoverDirectionCommand final : public EditorCommand {
public:
    SetLinearMoverDirectionCommand(std::string objectTypeId, Vec2 direction);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetLinearMoverDirection"; }

private:
    std::string objectTypeId_;
    Vec2 next_{};
    Vec2 previous_{};
    bool captured_ = false;
};

class SetLinearMoverSpeedCommand final : public EditorCommand {
public:
    SetLinearMoverSpeedCommand(std::string objectTypeId, float speed);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetLinearMoverSpeed"; }

private:
    std::string objectTypeId_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
