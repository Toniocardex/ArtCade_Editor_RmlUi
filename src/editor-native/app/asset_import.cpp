#include "editor-native/app/asset_import.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/image_asset_commands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <string>
#include <system_error>

namespace ArtCade::EditorNative {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

template <std::size_t N>
bool extIn(const std::string& extLower, const std::array<const char*, N>& set) {
    return std::find(set.begin(), set.end(), extLower) != set.end();
}

// Copy the source into <projectRoot>/<subdir> with a name unique against both the
// folder and the catalog (one suffix keeps the file name and the AssetId in
// step), forward-slash relative path for the document. The per-kind caller then
// records it with its typed command (and rolls the file back on failure).
struct CopiedAsset {
    AssetId               assetId;
    std::string           relativePath;
    std::filesystem::path destination;
};

bool copyUnique(const std::filesystem::path& projectRoot, const std::string& subdir,
                const std::filesystem::path& source,
                const std::function<bool(const AssetId&)>& idTaken,
                CopiedAsset& out, std::string& error) {
    const std::filesystem::path dir = projectRoot / "assets" / subdir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) { error = "Could not create assets/" + subdir + ": " + ec.message(); return false; }

    const std::string stem = source.stem().string();
    const std::string ext = source.extension().string();   // original case on disk
    std::string fileName = stem + ext;
    AssetId     assetId = stem;
    for (int n = 2; std::filesystem::exists(dir / fileName) || idTaken(assetId); ++n) {
        fileName = stem + "_" + std::to_string(n) + ext;
        assetId = stem + "_" + std::to_string(n);
    }

    out.destination = dir / fileName;
    std::filesystem::copy_file(source, out.destination, ec);
    if (ec) { error = "Could not copy file: " + ec.message(); return false; }
    out.assetId = assetId;
    out.relativePath = "assets/" + subdir + "/" + fileName;
    return true;
}

ImportAssetResult finish(EditorCoordinator& coordinator, const CopiedAsset& copied,
                         const EditorOperationResult& command) {
    if (!command.ok) {
        std::error_code ec;
        std::filesystem::remove(copied.destination, ec);   // roll back the copy
        return ImportAssetResult::failure("Import failed: " + command.error);
    }
    return ImportAssetResult::success(copied.assetId);
}

ImportAssetResult importImageAsset(EditorCoordinator& coordinator,
                                   const std::filesystem::path& projectRoot,
                                   const std::filesystem::path& source) {
    static constexpr std::array<const char*, 4> kExt{".png", ".jpg", ".jpeg", ".webp"};
    if (!extIn(toLower(source.extension().string()), kExt)) {
        return ImportAssetResult::failure("Unsupported image format");
    }
    CopiedAsset copied;
    std::string error;
    if (!copyUnique(projectRoot, "images", source,
                    [&](const AssetId& id) { return coordinator.document().hasImageAsset(id); },
                    copied, error)) {
        return ImportAssetResult::failure(error);
    }
    return finish(coordinator, copied,
                  coordinator.execute(AddImageAssetCommand{copied.assetId, copied.relativePath}));
}

ImportAssetResult importAudioAsset(EditorCoordinator& coordinator,
                                   const std::filesystem::path& projectRoot,
                                   const std::filesystem::path& source,
                                   const std::optional<AudioLoadMode>& mode) {
    static constexpr std::array<const char*, 3> kExt{".wav", ".ogg", ".mp3"};
    const std::string ext = toLower(source.extension().string());
    if (!extIn(ext, kExt)) {
        return ImportAssetResult::failure("Unsupported audio format");
    }
    // Default: short WAV into memory, compressed formats stream.
    const AudioLoadMode loadMode =
        mode.value_or(ext == ".wav" ? AudioLoadMode::StaticSound : AudioLoadMode::Stream);
    CopiedAsset copied;
    std::string error;
    if (!copyUnique(projectRoot, "audio", source,
                    [&](const AssetId& id) { return coordinator.document().hasAudioAsset(id); },
                    copied, error)) {
        return ImportAssetResult::failure(error);
    }
    return finish(coordinator, copied,
                  coordinator.execute(
                      AddAudioAssetCommand{copied.assetId, copied.relativePath, loadMode}));
}

ImportAssetResult importFontAsset(EditorCoordinator& coordinator,
                                  const std::filesystem::path& projectRoot,
                                  const std::filesystem::path& source) {
    static constexpr std::array<const char*, 2> kExt{".ttf", ".otf"};
    if (!extIn(toLower(source.extension().string()), kExt)) {
        return ImportAssetResult::failure("Unsupported font format");
    }
    CopiedAsset copied;
    std::string error;
    if (!copyUnique(projectRoot, "fonts", source,
                    [&](const AssetId& id) { return coordinator.document().hasFontAsset(id); },
                    copied, error)) {
        return ImportAssetResult::failure(error);
    }
    return finish(coordinator, copied,
                  coordinator.execute(AddFontAssetCommand{
                      copied.assetId, copied.relativePath, 32, FontGlyphPreset::European}));
}

} // namespace

ImportAssetResult importAsset(EditorCoordinator& coordinator,
                              const std::filesystem::path& projectRoot,
                              const ImportAssetRequest& request) {
    if (coordinator.isPlaying()) {
        return ImportAssetResult::failure("Stop Play before importing assets");
    }
    if (projectRoot.empty()) {
        return ImportAssetResult::failure("Save the project before importing assets");
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(request.sourcePath, ec)) {
        return ImportAssetResult::failure("Source file not found");
    }

    switch (request.kind) {
        case AssetKind::Image:
            return importImageAsset(coordinator, projectRoot, request.sourcePath);
        case AssetKind::Audio:
            return importAudioAsset(coordinator, projectRoot, request.sourcePath,
                                    request.audioMode);
        case AssetKind::Font:
            return importFontAsset(coordinator, projectRoot, request.sourcePath);
    }
    return ImportAssetResult::failure("Unknown asset kind");
}

} // namespace ArtCade::EditorNative
