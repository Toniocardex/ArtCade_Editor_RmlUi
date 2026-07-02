#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// PlatformerController is object-type owned, like the other movement drivers.
// This slice authors three values mapped onto the canonical component:
//   Move Speed -> maxSpeed, Jump Speed -> jumpForce, Gravity -> customGravity
// (coyoteTime, jumpBuffer and climbSpeed persist at their defaults, not edited).
//
// Single movement writer: Add fails if the object type already owns a
// TopDownController or a LinearMover (three drivers of the same transform would
// make the result depend on update order).
class AddPlatformerControllerCommand final : public EditorCommand {
public:
    explicit AddPlatformerControllerCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddPlatformerController"; }

private:
    std::string objectTypeId_;
};

class RemovePlatformerControllerCommand final : public EditorCommand {
public:
    explicit RemovePlatformerControllerCommand(std::string objectTypeId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemovePlatformerController"; }

private:
    std::string objectTypeId_;
    PlatformerControllerComponent removed_{};
    bool captured_ = false;
};

// Which scalar a Set command edits. One command class keeps the three numeric
// fields uniform (validate finite + >= 0, capture previous, exact undo).
enum class PlatformerField { MoveSpeed, JumpSpeed, Gravity };

class SetPlatformerValueCommand final : public EditorCommand {
public:
    SetPlatformerValueCommand(std::string objectTypeId, PlatformerField field, float value);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetPlatformerValue"; }

private:
    std::string objectTypeId_;
    PlatformerField field_;
    float next_ = 0.f;
    float previous_ = 0.f;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
