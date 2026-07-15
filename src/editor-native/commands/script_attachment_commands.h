#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <optional>

namespace ArtCade::EditorNative {

class AddScriptAttachmentCommand final : public EditorCommand {
public:
    AddScriptAttachmentCommand(ObjectTypeId objectTypeId,
                               ScriptAttachmentDef attachment,
                               std::size_t index);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "AddScriptAttachment"; }
private:
    ObjectTypeId objectTypeId_;
    ScriptAttachmentDef attachment_;
    std::size_t index_ = 0;
    std::optional<ScriptComponent> before_;
    bool captured_ = false;
};

class RemoveScriptAttachmentCommand final : public EditorCommand {
public:
    RemoveScriptAttachmentCommand(ObjectTypeId objectTypeId,
                                  ScriptAttachmentId attachmentId);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "RemoveScriptAttachment"; }
private:
    ObjectTypeId objectTypeId_;
    ScriptAttachmentId attachmentId_;
    std::optional<ScriptComponent> before_;
    bool captured_ = false;
};

class MoveScriptAttachmentCommand final : public EditorCommand {
public:
    MoveScriptAttachmentCommand(ObjectTypeId objectTypeId,
                                ScriptAttachmentId attachmentId,
                                std::size_t destinationIndex);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "MoveScriptAttachment"; }
private:
    ObjectTypeId objectTypeId_;
    ScriptAttachmentId attachmentId_;
    std::size_t destinationIndex_ = 0;
    std::optional<ScriptComponent> before_;
    bool captured_ = false;
};

class SetScriptAttachmentEnabledCommand final : public EditorCommand {
public:
    SetScriptAttachmentEnabledCommand(ObjectTypeId objectTypeId,
                                      ScriptAttachmentId attachmentId,
                                      bool enabled);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "SetScriptAttachmentEnabled"; }
private:
    ObjectTypeId objectTypeId_;
    ScriptAttachmentId attachmentId_;
    bool enabled_ = true;
    std::optional<ScriptComponent> before_;
    bool captured_ = false;
};

ScriptAttachmentId nextScriptAttachmentId(const ScriptComponent& scripts);

} // namespace ArtCade::EditorNative
