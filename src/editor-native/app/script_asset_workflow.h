#pragma once

#include "core/types.h"

#include <filesystem>
#include <functional>
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

using ScriptDirtyBufferQuery = std::function<bool(const AssetId&)>;

ScriptAssetWorkflowResult createScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    std::string baseName = "NewScript");

ScriptAssetWorkflowResult importScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    const std::filesystem::path& sourcePath);

// Deletes ScriptAssetDef metadata and the confined .lua source together.
// filesystem + RemoveScriptAssetCommand share one history entry via side effect.
// dirtyBufferQuery rejects Redo while an open buffer is dirty.
ScriptAssetWorkflowResult removeScriptAsset(
    EditorCoordinator& coordinator,
    const std::filesystem::path& projectRoot,
    const AssetId& assetId,
    ScriptDirtyBufferQuery dirtyBufferQuery);

} // namespace ArtCade::EditorNative
