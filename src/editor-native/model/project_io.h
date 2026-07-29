#pragma once

#include "editor-native/model/project_document.h"

#include <string>
#include <string_view>
#include <utility>

namespace ArtCade::EditorNative {

template <class T>
struct ProjectIoResult {
    bool        ok = false;
    T           value{};
    std::string error;

    static ProjectIoResult success(T result) {
        ProjectIoResult out;
        out.ok = true;
        out.value = std::move(result);
        return out;
    }

    static ProjectIoResult failure(std::string message) {
        ProjectIoResult out;
        out.error = std::move(message);
        return out;
    }
};

using DeserializeResult = ProjectIoResult<ProjectDocument>;
using SerializeResult = ProjectIoResult<std::string>;

// Same constant used by serializer / migration / validation.
int currentProjectSchemaVersion();

// Creates an unsaved authoring document in the only schema this alpha accepts.
// Callers that load external data must use ProjectSerializer instead, so an
// unsupported format is rejected rather than normalized in memory.
ProjectDoc makeNewProjectData();

class ProjectSerializer {
public:
    static DeserializeResult deserialize(std::string_view source);
    static SerializeResult serialize(const ProjectDocument& document);
};

class ProjectMigration {
public:
    static DeserializeResult migrate(ProjectDocument document);
};

class ProjectValidator {
public:
    static DeserializeResult validate(ProjectDocument document);
};

} // namespace ArtCade::EditorNative
