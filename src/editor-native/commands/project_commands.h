#pragma once

#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

/** Rename the project metadata stored in ProjectDocument.projectName. */
class RenameProjectCommand final : public EditorCommand {
public:
    explicit RenameProjectCommand(std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameProject"; }

private:
    std::string next_;
    std::string previous_;
    bool        captured_ = false;
};

} // namespace ArtCade::EditorNative
