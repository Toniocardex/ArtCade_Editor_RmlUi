#include "editor-native/app/project_load.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/project_io.h"

#include <utility>

namespace ArtCade::EditorNative {

ProjectLoadResult loadProjectFromText(EditorCoordinator& coordinator,
                                      std::string_view source) {
    DeserializeResult deserialized = ProjectSerializer::deserialize(source);
    if (!deserialized.ok) {
        return ProjectLoadResult::failure(ProjectLoadStage::Deserialize,
                                          std::move(deserialized.error));
    }

    DeserializeResult migrated = ProjectMigration::migrate(std::move(deserialized.value));
    if (!migrated.ok) {
        return ProjectLoadResult::failure(ProjectLoadStage::Migration,
                                          std::move(migrated.error));
    }

    DeserializeResult validated = ProjectValidator::validate(std::move(migrated.value));
    if (!validated.ok) {
        return ProjectLoadResult::failure(ProjectLoadStage::Validation,
                                          std::move(validated.error));
    }

    EditorOperationResult replaced = coordinator.replaceProject(std::move(validated.value));
    if (!replaced.ok) {
        return ProjectLoadResult::failure(ProjectLoadStage::Replace, replaced.error);
    }
    return ProjectLoadResult::success(std::move(replaced));
}

} // namespace ArtCade::EditorNative
