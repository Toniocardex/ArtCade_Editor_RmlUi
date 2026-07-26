#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

/**
 * Atomically replace the Object Type TextComponent (or remove when nullopt).
 * Same family as SetObjectTypeSpritePresentationCommand.
 */
class SetObjectTypeTextComponentCommand final : public EditorCommand {
public:
    SetObjectTypeTextComponentCommand(ObjectTypeId objectTypeId,
                                      std::optional<TextComponent> next);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetObjectTypeTextComponent"; }

private:
    ObjectTypeId objectTypeId_;
    std::optional<TextComponent> next_;
    std::optional<TextComponent> previous_;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
