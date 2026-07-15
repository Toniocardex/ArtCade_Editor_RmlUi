#pragma once

#include "core/types.h"

#include <filesystem>
#include <string>
#include <utility>

namespace ArtCade::EditorNative {

class EditorCoordinator;

struct ScriptAssetWorkflowResult {
    bool        ok = false;
    AssetId     assetId;
    std::string error;

    static ScriptAssetWorkflowResult success(AssetId id) {
        return {true, std::move(id), {}};
    }
    static ScriptAssetWorkflowResult failure(std::string error) {
        return {false, {}, std::move(error)};
    }
};

ScriptAssetWorkflowResult createScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    std::string baseName = "NewScript");

ScriptAssetWorkflowResult importScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    const std::filesystem::path& sourcePath);

} // namespace ArtCade::EditorNative

