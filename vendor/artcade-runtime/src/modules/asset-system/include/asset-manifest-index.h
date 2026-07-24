#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ArtCade::Modules {

/**
 * Stable asset id ↔ project-relative path index (Phase C dual-read).
 * Matches editor resolve-image-load-key semantics.
 */
class AssetManifestIndex {
public:
    void clear();

    /** Parse export manifest.json (assets[] with id, type, relativePath). */
    bool loadFromJsonFile(const std::string& manifestPath);

    void addImageEntry(const std::string& id, const std::string& relativePath);
    void addAudioEntry(const std::string& id, const std::string& relativePath);
    void addFontEntry(const std::string& id, const std::string& relativePath);

    /** Resolve sprite / texture key to TextureCache path (§3.4). */
    std::string resolveImageKey(const std::string& ref) const;

    std::string resolveAudioKey(const std::string& ref) const;
    std::string resolveFontKey(const std::string& ref) const;

    bool empty() const {
        return imageIdToPath_.empty() && imagePaths_.empty() &&
               audioIdToPath_.empty() && audioPaths_.empty() &&
               fontIdToPath_.empty() && fontPaths_.empty();
    }

private:
    std::unordered_map<std::string, std::string> imageIdToPath_;
    std::unordered_map<std::string, std::string> audioIdToPath_;
    std::unordered_map<std::string, std::string> fontIdToPath_;
    std::unordered_set<std::string> imagePaths_;
    std::unordered_set<std::string> audioPaths_;
    std::unordered_set<std::string> fontPaths_;

    static std::string resolveFromMaps(
        const std::string& ref,
        const std::unordered_map<std::string, std::string>& idToPath,
        const std::unordered_set<std::string>& knownPaths);
};

} // namespace ArtCade::Modules
