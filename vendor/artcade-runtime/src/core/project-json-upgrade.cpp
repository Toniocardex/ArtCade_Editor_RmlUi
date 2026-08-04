#include "project-json-upgrade.h"

#include "project-current-format.h"

namespace ArtCade::ProjectJson {
namespace {

int readFormatVersion(const nlohmann::json& root) {
    if (!root.is_object()) return -1;
    if (root.contains("formatVersion") && root["formatVersion"].is_number_integer())
        return root["formatVersion"].get<int>();
    if (root.contains("format_version") && root["format_version"].is_number_integer())
        return root["format_version"].get<int>();
    return -1;
}

void upgradeObjectTypePresentationsV12ToV13(nlohmann::json& root) {
    if (!root.contains("objectTypes")) return;
    auto& objectTypes = root["objectTypes"];
    auto addPivot = [](nlohmann::json& presentation) {
        if (!presentation.is_object()) return;
        if (presentation.contains("pivot")) return;
        presentation["pivot"] = nlohmann::json{{"x", 0.5}, {"y", 0.5}};
    };

    if (objectTypes.is_array()) {
        for (auto& item : objectTypes) {
            if (!item.is_object() || !item.contains("spritePresentation")) continue;
            addPivot(item["spritePresentation"]);
        }
    } else if (objectTypes.is_object()) {
        for (auto& [key, item] : objectTypes.items()) {
            (void)key;
            if (!item.is_object() || !item.contains("spritePresentation")) continue;
            addPivot(item["spritePresentation"]);
        }
    }
}

} // namespace

ProjectJsonUpgradeResult upgradeProjectJsonToCurrent(const nlohmann::json& source) {
    ProjectJsonUpgradeResult result;
    if (!source.is_object()) {
        result.error = "Project root must be a JSON object";
        return result;
    }

    int version = readFormatVersion(source);
    if (version < 0) {
        result.error = "Project formatVersion is missing or invalid";
        return result;
    }

    if (version == kCurrentProjectFormatVersion) {
        result.ok = true;
        result.changed = false;
        result.root = source;
        return result;
    }

    result.root = source;
    if (version == 12) {
        upgradeObjectTypePresentationsV12ToV13(result.root);
        result.root["formatVersion"] = 13;
        result.root["schemaVersion"] = 13;
        version = 13;
    }

    // ADR-0058 adds a sparse optional field. Existing v13 documents require
    // no data rewrite beyond advancing the schema marker.
    if (version == 13) {
        result.root["formatVersion"] = 14;
        result.root["schemaVersion"] = 14;
        version = 14;
    }
    if (version == kCurrentProjectFormatVersion) {
        result.ok = true;
        result.changed = true;
        return result;
    }

    result.error = "Unsupported project schema version "
        + std::to_string(version) + " (current is "
        + std::to_string(kCurrentProjectFormatVersion) + ")";
    return result;
}

} // namespace ArtCade::ProjectJson
