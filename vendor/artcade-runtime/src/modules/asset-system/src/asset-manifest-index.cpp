#include "../include/asset-manifest-index.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace ArtCade::Modules {

void AssetManifestIndex::clear() {
    imageIdToPath_.clear();
    audioIdToPath_.clear();
    fontIdToPath_.clear();
    imagePaths_.clear();
    audioPaths_.clear();
    fontPaths_.clear();
}

void AssetManifestIndex::addImageEntry(const std::string& id,
                                       const std::string& relativePath) {
    if (id.empty() || relativePath.empty()) return;
    imageIdToPath_[id] = relativePath;
    imagePaths_.insert(relativePath);
}

void AssetManifestIndex::addAudioEntry(const std::string& id,
                                     const std::string& relativePath) {
    if (id.empty() || relativePath.empty()) return;
    audioIdToPath_[id] = relativePath;
    audioPaths_.insert(relativePath);
}

void AssetManifestIndex::addFontEntry(const std::string& id,
                                     const std::string& relativePath) {
    if (id.empty() || relativePath.empty()) return;
    fontIdToPath_[id] = relativePath;
    fontPaths_.insert(relativePath);
}

bool AssetManifestIndex::loadFromJsonFile(const std::string& manifestPath) {
    std::ifstream f(manifestPath);
    if (!f) return false;

    json j;
    try {
        j = json::parse(f);
    } catch (...) {
        return false;
    }

    if (!j.contains("assets") || !j["assets"].is_array()) return true;

    for (const auto& entry : j["assets"]) {
        if (!entry.is_object()) continue;
        const std::string type = entry.value("type", std::string{});
        const std::string id   = entry.value("id", std::string{});
        const std::string path = entry.value(
            "relativePath",
            entry.value("relative_path", std::string{}));
        if (type == "image") addImageEntry(id, path);
        else if (type == "audio") addAudioEntry(id, path);
        else if (type == "font") addFontEntry(id, path);
    }
    return true;
}

std::string AssetManifestIndex::resolveFromMaps(
    const std::string& ref,
    const std::unordered_map<std::string, std::string>& idToPath,
    const std::unordered_set<std::string>& knownPaths)
{
    if (ref.empty()) return ref;
    const auto idIt = idToPath.find(ref);
    if (idIt != idToPath.end()) return idIt->second;
    if (knownPaths.find(ref) != knownPaths.end()) return ref;
    return ref;
}

std::string AssetManifestIndex::resolveImageKey(const std::string& ref) const {
    return resolveFromMaps(ref, imageIdToPath_, imagePaths_);
}

std::string AssetManifestIndex::resolveAudioKey(const std::string& ref) const {
    return resolveFromMaps(ref, audioIdToPath_, audioPaths_);
}

std::string AssetManifestIndex::resolveFontKey(const std::string& ref) const {
    return resolveFromMaps(ref, fontIdToPath_, fontPaths_);
}

} // namespace ArtCade::Modules
