#pragma once

#include "editor-native/model/play_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// One Sound to LoadSound at Start Play. Metadata only — no Raylib types.
struct PlaySoundPreloadEntry {
    AssetId assetId;
    std::string sourcePath;
    std::filesystem::path resolvedPath;
};

// Plans preload from PlaySession::assets().audioAssets only (referenced StaticSound
// entries). Confines each path under assetRoot and checks the file exists.
// Does not open/decode WAV and never touches ProjectDocument.
// On failure: *out is empty and *error (if provided) explains why Play must not start.
bool planPlaySoundPreload(const PlayAssetCatalogSnapshot& assets,
                          const std::filesystem::path& assetRoot,
                          std::vector<PlaySoundPreloadEntry>* out,
                          std::string* error = nullptr);

} // namespace ArtCade::EditorNative
