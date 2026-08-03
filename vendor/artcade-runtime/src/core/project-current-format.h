#pragma once

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ArtCade::ProjectJson {

/** Current persisted project contract. Older formats are rejected after the
 *  shared upgrader (upgradeProjectJsonToCurrent) has run — only the current
 *  version is accepted by validate_current_project_json. */
inline constexpr int kCurrentProjectFormatVersion = 13;

/**
 * Validates the current JSON document contract before any parser consumes it.
 * @param root          decoded project.json root object.
 * @param error_message receives a user-facing rejection reason on failure.
 * @returns true only for a complete kCurrentProjectFormatVersion ProjectDoc document.
 */
bool validate_current_project_json(const nlohmann::json &root, std::string &error_message);

/**
 * Validates the in-memory persisted layer contract before writing it to disk.
 * @param document      ProjectDoc to serialize.
 * @param error_message receives a user-facing rejection reason on failure.
 * @returns true only when every scene has internally consistent layer identity.
 */
bool validate_current_project_document(const ProjectDoc &document, std::string &error_message);

} // namespace ArtCade::ProjectJson
