#pragma once

#include "core/types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class EditorCoordinator;

// The single canonical import pipeline. Every UI trigger (Assets panel, a future
// File > Import, drag-and-drop, Inspector > Import Image) builds an
// ImportAssetRequest and calls importAsset — there is no per-UI import path. The
// authoring mutation still goes through a typed command (AddImageAssetCommand,
// and later AddAudioAssetCommand / AddFontAssetCommand): the single entry point
// is about the operation, not about erasing the per-kind domain.
enum class AssetKind { Image, Audio, Font };

struct ImportAssetRequest {
    AssetKind             kind = AssetKind::Image;
    std::filesystem::path sourcePath;
    // For audio: optional explicit load mode; when unset a sensible per-extension
    // default is used (wav -> StaticSound, ogg/mp3 -> Stream).
    std::optional<AudioLoadMode> audioMode;
};

struct ImportAssetResult {
    bool        ok = false;
    AssetId     assetId;   // catalog id of the imported asset, on success
    std::string error;

    static ImportAssetResult success(AssetId id) { return {true, std::move(id), {}}; }
    static ImportAssetResult failure(std::string message) {
        return {false, {}, std::move(message)};
    }
};

// Common pipeline: reject during Play, require a saved project, validate the
// source, copy into a portable destination under projectRoot with a unique name,
// run the per-kind command, and roll the copied file back if the command fails.
// `projectRoot` is the directory of the saved project (empty if unsaved).
ImportAssetResult importAsset(EditorCoordinator& coordinator,
                              const std::filesystem::path& projectRoot,
                              const ImportAssetRequest& request);

} // namespace ArtCade::EditorNative
