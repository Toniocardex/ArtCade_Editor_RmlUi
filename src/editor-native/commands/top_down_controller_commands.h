#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// TopDownController is object-type owned (canonical gameplay component). This
// slice authors the movement speed (the canonical maxSpeed); acceleration,
// friction and fourDirections persist at their defaults and are not edited yet.
class AddTopDownControllerCommand final : public EditorCommand {
public:
    explicit AddTopDownControllerCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddTopDownController"; }

private:
    std::string objectTypeId_;
};

class RemoveTopDownControllerCommand final : public EditorCommand {
public:
    explicit RemoveTopDownControllerCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveTopDownController"; }

private:
    std::string objectTypeId_;
    TopDownControllerComponent removed_{};
    bool captured_ = false;
};

class SetTopDownControllerSpeedCommand final : public EditorCommand {
public:
    SetTopDownControllerSpeedCommand(std::string objectTypeId, float speed);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTopDownControllerSpeed"; }

private:
    std::string objectTypeId_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
