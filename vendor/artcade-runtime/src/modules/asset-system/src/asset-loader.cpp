#include "../include/asset-loader.h"
#include "zip-reader.h"
#include "object-type-materialize.h"
#include "asset-json.h"
#include "collision-json.h"
#include "entity-json.h"
#include "scene-json.h"
#include "project-meta-json.h"
#include "project-current-format.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

namespace ArtCade::Modules {

namespace {

std::optional<std::filesystem::path> createExtractionDirectory() {
    namespace fs = std::filesystem;
    std::random_device random;
    std::error_code ec;
    const fs::path tempRoot = fs::temp_directory_path(ec);
    if (ec) return std::nullopt;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto nonce = (static_cast<uint64_t>(random()) << 32u) ^ random();
        const fs::path candidate = tempRoot / ("artcade_" + std::to_string(nonce));
        if (fs::create_directory(candidate, ec)) return candidate;
        if (ec && ec != std::errc::file_exists) return std::nullopt;
        ec.clear();
    }
    return std::nullopt;
}

std::optional<std::string> resolveUnderRoot(const std::string& rootPath,
                                            const std::string& relativePath) {
    namespace fs = std::filesystem;
    const fs::path relative(relativePath);
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& component : relative) {
        if (component.string().find(':') != std::string::npos)
            return std::nullopt;
        if (component == "." || component == ".." || component.empty())
            return std::nullopt;
    }
    std::error_code ec;
    const fs::path root = fs::canonical(rootPath, ec);
    if (ec) return std::nullopt;
    const fs::path candidate = root / relative;
    fs::path resolved = fs::weakly_canonical(candidate, ec);
    if (ec) return std::nullopt;
    const fs::path withinRoot = resolved.lexically_relative(root);
    const auto first = withinRoot.begin();
    if (withinRoot.empty() || withinRoot.is_absolute() ||
        first == withinRoot.end() || *first == "..")
        return std::nullopt;
    return resolved.string();
}

} // namespace

// ------------------------------------------------------------------ lifecycle

bool AssetLoader::init()     { return true; }
void AssetLoader::shutdown() {
    if (!extractTempDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(extractTempDir_, ec);
        extractTempDir_.clear();
    }
}

// ------------------------------------------------------------------ loaders

void AssetLoader::loadManifestForRoot(const std::string& rootPath) {
    manifestIndex_.clear();
    manifestIndex_.loadFromJsonFile(rootPath + "/manifest.json");
}

std::string AssetLoader::resolveImagePath(const std::string& ref) const {
    const std::string relative = manifestIndex_.resolveImageKey(ref);
    const auto resolved = resolveUnderRoot(rootPath_, relative);
    if (relative == ref && ref.find_first_of("/\\:") == std::string::npos &&
        resolved && !std::filesystem::exists(*resolved))
        return ref; // Editor-uploaded texture key, not a filesystem path.
    return resolved.value_or(std::string{});
}

std::string AssetLoader::resolveAudioPath(const std::string& ref) const {
    const std::string relative = manifestIndex_.resolveAudioKey(ref);
    const auto resolved = resolveUnderRoot(rootPath_, relative);
    return resolved.value_or(std::string{});
}

bool AssetLoader::loadDirectory(const std::string& dirPath, ProjectDoc& out) {
    extractTempDir_.clear();
    rootPath_ = dirPath;
    devMode_  = true;
    loadManifestForRoot(dirPath);
    try {
        if (!parseProjectJson(dirPath + "/project.json", out)) return false;
        parseGameJson(dirPath + "/game.json", out);
    } catch (...) {
        return false;
    }
    return true;
}

bool AssetLoader::loadArtcade(const std::string& archivePath, ProjectDoc& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!extractTempDir_.empty()) {
        fs::remove_all(extractTempDir_, ec);
        extractTempDir_.clear();
        ec.clear();
    }
    const auto tempDir = createExtractionDirectory();
    if (!tempDir) return false;
    const std::string tmpDir = tempDir->string();
    if (!extractZip(archivePath, tmpDir)) {
        fs::remove_all(*tempDir, ec);
        return false;
    }

    extractTempDir_ = tmpDir;
    rootPath_ = tmpDir;
    devMode_  = false;
    loadManifestForRoot(tmpDir);
    try {
        if (!parseProjectJson(tmpDir + "/project.json", out)) return false;
        parseGameJson(tmpDir + "/game.json", out);
    } catch (...) {
        return false;
    }
    return true;
}

bool AssetLoader::loadLuaBytecode(const std::string& path,
                                   std::vector<uint8_t>& outBytes) {
    const auto fullPath = resolveUnderRoot(rootPath_, path);
    if (!fullPath) return false;

    std::ifstream f(*fullPath, std::ios::binary);
    if (!f) return false;
    outBytes.assign(std::istreambuf_iterator<char>(f),
                    std::istreambuf_iterator<char>());
    return !outBytes.empty();
}

bool AssetLoader::loadScriptSource(const std::string& path, std::size_t maxBytes,
                                   std::string& outSource) {
    outSource.clear();
    if (maxBytes == 0) return false;
    const auto fullPath = resolveUnderRoot(rootPath_, path);
    if (!fullPath) return false;
    std::ifstream file(*fullPath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > maxBytes) return false;
    outSource.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(outSource.data(), size)) {
        outSource.clear();
        return false;
    }
    return true;
}

std::string AssetLoader::resolveAssetPath(const std::string& assetId,
                                          const std::string& assetType) const {
    const auto resolved = resolveUnderRoot(
        rootPath_, "assets/" + assetType + "/" + assetId);
    return resolved.value_or(std::string{});
}

std::string AssetLoader::resolveFontPath(const std::string& ref) const {
    const std::string relative = manifestIndex_.resolveFontKey(ref);
    const auto resolved = resolveUnderRoot(rootPath_, relative);
    if (relative == ref && ref.find_first_of("/\\:") == std::string::npos &&
        resolved && !std::filesystem::exists(*resolved))
        return ref;
    return resolved.value_or(std::string{});
}

// ------------------------------------------------------------------ JSON parsing

bool AssetLoader::parseProjectJson(const std::string& path, ProjectDoc& out) {
    imagePointsByAsset_.clear();
    // project.json assets augment manifest (dev mode may have no manifest.json).
    std::ifstream f(path);
    if (!f) return false;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    std::string validation_error;
    if (!ProjectJson::validate_current_project_json(j, validation_error)) return false;

    ProjectJson::read_project_header(j, out);
    ProjectJson::read_global_variables(j, out);

    if (j.contains("world") && j["world"].is_object())
        ProjectJson::read_world_settings(j["world"], out.world);

    ProjectJson::read_object_types_map(j, out.objectTypes);
    // RU-01: shared with the editor's canonical project reader now - was
    // inline here only, so the editor would have had to duplicate it.
    if (!ProjectJson::read_object_type_logic_boards(j, out.objectTypes)) return false;
    ProjectJson::read_entities_map(j, out.entities, false);
    ProjectJson::read_scenes_map(j, out.scenes);
    ProjectJson::read_thumbnails(j, out.thumbnails);
    ProjectJson::read_physics_layers(j, out.physicsLayers);
    ProjectJson::read_collision_profiles(j, out.collisionProfiles);
    ProjectJson::read_tile_palette(j, out.tilePalette);
    ProjectJson::read_tilesets(j, out.tilesets);

    // ADR-0010: Animation-source sheet ids are resolved during materialise
    // from spriteAnimationAssets. Load that catalog before materialising
    // entities; otherwise Play/export leave imageAssetId empty and depend on
    // maybePlaySpawnClip (which also needs a non-empty defaultClipId).
    ProjectJson::read_sprite_animation_assets(j, out.spriteAnimationAssets);

    if (!ProjectJson::validate_current_project_document(out, validation_error)) return false;

    materializeProjectEntities(out);

    if (j.contains("assets") && j["assets"].is_object()) {
        for (auto& [key, av] : j["assets"].items()) {
            if (!av.is_object()) continue;
            const std::string libId   = av.value("id", key);
            const std::string libPath = av.value("path", std::string{});
            if (!libPath.empty())
                out.spritePathToAssetId[libPath] = libId.empty() ? key : libId;
            if (!libPath.empty())
                manifestIndex_.addImageEntry(libId, libPath);
            ImageAssetDef ad;
            ProjectJson::read_image_asset(av, key, ad);
            out.imageAssets.push_back(ad);
            if (!ad.imagePoints.empty())
                imagePointsByAsset_[ad.assetId] = ad.imagePoints;
        }
    }

    if (j.contains("imageAssets") && j["imageAssets"].is_array()) {
        ProjectJson::read_image_assets(j, out.imageAssets);
        for (const ImageAssetDef& asset : out.imageAssets) {
            if (!asset.sourcePath.empty()) {
                manifestIndex_.addImageEntry(asset.assetId, asset.sourcePath);
                out.spritePathToAssetId[asset.sourcePath] = asset.assetId;
            }
        }
    }

    ProjectJson::read_audio_assets(j, out.audioAssets);
    for (const AudioAssetDef& asset : out.audioAssets) {
        if (!asset.sourcePath.empty())
            manifestIndex_.addAudioEntry(asset.assetId, asset.sourcePath);
    }

    if (j.contains("fontAssets") && j["fontAssets"].is_object()) {
        for (auto& [key, av] : j["fontAssets"].items()) {
            if (!av.is_object()) continue;
            const std::string id = av.value("id", key);
            const std::string assetPath = av.value("path", std::string{});
            if (!assetPath.empty()) manifestIndex_.addFontEntry(id, assetPath);
        }
    } else if (j.contains("fontAssets") && j["fontAssets"].is_array()) {
        ProjectJson::read_font_assets(j, out.fontAssets);
        for (const FontAssetDef& asset : out.fontAssets) {
            if (!asset.sourcePath.empty())
                manifestIndex_.addFontEntry(asset.assetId, asset.sourcePath);
        }
    }

    resolveSpritePivotsFromImageAssets(out);

    return true;
}

std::optional<Vec2> AssetLoader::getImagePoint(const std::string& assetRef,
                                                const std::string& pointId) const {
    const std::string pathKey = manifestIndex_.resolveImageKey(assetRef);
    auto it = imagePointsByAsset_.find(pathKey);
    if (it == imagePointsByAsset_.end() && pathKey != assetRef)
        it = imagePointsByAsset_.find(assetRef);
    if (it == imagePointsByAsset_.end()) return std::nullopt;
    for (const auto& p : it->second) {
        if (p.id == pointId) return Vec2{ p.x, p.y };
    }
    return std::nullopt;
}

bool AssetLoader::parseGameJson(const std::string& path, ProjectDoc& out) {
    std::ifstream f(path);
    if (!f) return true;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    out.targetFPS = j.value("targetFPS", out.targetFPS);
    out.licenseTier = j.value("licenseTier", out.licenseTier);
    return true;
}

bool AssetLoader::extractZip(const std::string& zipPath, const std::string& destDir) {
    return ArtCade::zipExtractAll(zipPath, destDir);
}

} // namespace ArtCade::Modules
