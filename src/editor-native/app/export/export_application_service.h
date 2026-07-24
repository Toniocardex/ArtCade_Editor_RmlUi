#pragma once

#include "editor-native/app/export/export_types.h"
#include "editor-native/app/export/runtime_template_catalog.h"
#include "editor-native/model/project_document.h"

#include <filesystem>
#include <functional>

namespace ArtCade::EditorNative {

class EditorCoordinator;

struct ExportApplicationDeps {
    std::filesystem::path exportTemplatesRoot;
    std::function<bool()> projectFilesStableForSnapshot;
};

class ExportApplicationService {
public:
    explicit ExportApplicationService(ExportApplicationDeps deps);

    ExportResult exportWindows(const ExportRequest& request,
                               const ExportContext& context,
                               const ProjectDocument& document,
                               EditorCoordinator& coordinator) const;

private:
    ExportApplicationDeps deps_;
};

} // namespace ArtCade::EditorNative
