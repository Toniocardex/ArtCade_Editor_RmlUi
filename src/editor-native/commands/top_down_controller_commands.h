#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// TopDownController is object-type owned (canonical gameplay component).
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

class SetTopDownControllerAccelerationCommand final : public EditorCommand {
public:
    SetTopDownControllerAccelerationCommand(std::string objectTypeId, float acceleration);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTopDownControllerAcceleration"; }

private:
    std::string objectTypeId_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

class SetTopDownControllerFrictionCommand final : public EditorCommand {
public:
    SetTopDownControllerFrictionCommand(std::string objectTypeId, float friction);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTopDownControllerFriction"; }

private:
    std::string objectTypeId_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

class SetTopDownControllerFourDirectionsCommand final : public EditorCommand {
public:
    SetTopDownControllerFourDirectionsCommand(std::string objectTypeId, bool fourDirections);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTopDownControllerFourDirections"; }

private:
    std::string objectTypeId_;
    bool next_ = false;
    bool previous_ = false;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
