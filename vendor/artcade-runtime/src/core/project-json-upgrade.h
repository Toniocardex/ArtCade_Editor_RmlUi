#pragma once
// Shared project JSON upgrade to the current format version.
// Runs before validate_current_project_json (editor + AssetLoader + export).

#include <nlohmann/json.hpp>
#include <string>

namespace ArtCade::ProjectJson {

struct ProjectJsonUpgradeResult {
    bool ok = false;
    bool changed = false;
    nlohmann::json root;
    std::string error;
};

/**
 * Upgrade source JSON to kCurrentProjectFormatVersion when possible.
 * - formatVersion 12 → 13: add center pivot to every Object Type Sprite Presentation
 * - formatVersion == current → ok, changed=false
 * - other versions → ok=false
 */
ProjectJsonUpgradeResult upgradeProjectJsonToCurrent(const nlohmann::json& source);

} // namespace ArtCade::ProjectJson
