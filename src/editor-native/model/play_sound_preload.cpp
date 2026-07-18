#include "editor-native/model/play_sound_preload.h"

#include "editor-native/model/path_confinement.h"

#include <system_error>

namespace ArtCade::EditorNative {

bool planPlaySoundPreload(const PlayAssetCatalogSnapshot& assets,
                          const std::filesystem::path& assetRoot,
                          std::vector<PlaySoundPreloadEntry>* out,
                          std::string* error) {
    if (!out) {
        if (error) *error = "Cannot prepare audio: preload output is missing";
        return false;
    }
    out->clear();
    out->reserve(assets.audioAssets.size());

    for (const auto& [assetId, asset] : assets.audioAssets) {
        if (asset.loadMode != AudioLoadMode::StaticSound) {
            if (error) {
                *error = "Cannot start Play: Play Sound requires StaticSound: "
                       + assetId;
            }
            out->clear();
            return false;
        }

        const PathConfinementResult resolved = resolvePathInsideRoot(
            assetRoot, std::filesystem::u8path(asset.sourcePath));
        if (!resolved.ok) {
            if (error) {
                *error = "Cannot start Play: invalid audio asset path for "
                       + assetId + ": " + resolved.error;
            }
            out->clear();
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(resolved.value, ec) || ec) {
            if (error) {
                *error = "Cannot start Play: audio file is missing: "
                       + asset.sourcePath;
            }
            out->clear();
            return false;
        }

        out->push_back(PlaySoundPreloadEntry{
            assetId, asset.sourcePath, resolved.value});
    }
    return true;
}

} // namespace ArtCade::EditorNative
