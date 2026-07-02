#include "editor-native/commands/project_commands.h"

#include "editor-native/model/project_document.h"

#include <utility>

namespace ArtCade::EditorNative {

RenameProjectCommand::RenameProjectCommand(std::string name)
    : next_(std::move(name)) {}

EditorOperationResult RenameProjectCommand::apply(ProjectDocument& document) {
    if (next_.empty()) {
        return EditorOperationResult::failure("Project name cannot be empty");
    }
    if (document.data().projectName == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = document.data().projectName;
        captured_ = true;
    }
    if (!document.setProjectName(next_)) {
        return EditorOperationResult::failure("Failed to rename project");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Inspector | EditorInvalidation::Toolbar | EditorInvalidation::Project,
        DomainChange::projectChanged());
}

EditorOperationResult RenameProjectCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setProjectName(previous_)) {
        return EditorOperationResult::failure("Cannot undo project rename");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Inspector | EditorInvalidation::Toolbar | EditorInvalidation::Project,
        DomainChange::projectChanged());
}

} // namespace ArtCade::EditorNative
