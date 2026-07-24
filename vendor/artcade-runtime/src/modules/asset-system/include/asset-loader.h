#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include "asset-manifest-index.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace ArtCade::Modules {

/**
 * AssetLoader — project loading and asset path resolution.
 *
 * Supports two modes:
 *   - Dev  : loose directory (fast iteration)
 *   - Ship : packed .artcade ZIP (single-file distribution)
 *
 * Asset caching (GPU textures, audio handles) is the responsibility of
 * the Renderer and Audio modules; AssetLoader only handles disk I/O.
 */
class AssetLoader final : public IModule {
public:
    AssetLoader() = default;

    bool init() override;
    void shutdown() override;

    // Load from packed .artcade archive
    bool loadArtcade(const std::string& archivePath, ProjectDoc& outDoc);

    // Load from loose directory (dev mode)
    bool loadDirectory(const std::string& dirPath, ProjectDoc& outDoc);

    // Load raw Lua bytecode
    bool loadLuaBytecode(const std::string& scriptPath,
                         std::vector<uint8_t>& outBytes);
    // Confined source read for immutable manual-script snapshots. Works for
    // loose projects and extracted .artcade packages; never exposes a path to Lua.
    bool loadScriptSource(const std::string& scriptPath, std::size_t maxBytes,
                          std::string& outSource);

    // Resolve asset file path (works in both dev and packed mode)
    std::string resolveAssetPath(const std::string& assetId,
                                 const std::string& assetType) const;

    /** Dual-read: stable image id or legacy path → project-relative texture key. */
    std::string resolveImagePath(const std::string& ref) const;

    /** Dual-read for audio keys. */
    std::string resolveAudioPath(const std::string& ref) const;

    /** Dual-read for font keys. */
    std::string resolveFontPath(const std::string& ref) const;

    const AssetManifestIndex& manifestIndex() const { return manifestIndex_; }

    bool isDevMode() const { return devMode_; }

    /** Project root (directory or extracted .artcade temp dir). */
    const std::string& projectRoot() const { return rootPath_; }

    /** Normalised image point 0..1 on asset path; empty if unknown. */
    std::optional<Vec2> getImagePoint(const std::string& assetPath,
                                      const std::string& pointId) const;

private:
    bool        devMode_  = false;
    std::string rootPath_;
    std::string extractTempDir_;
    AssetManifestIndex manifestIndex_;
    std::unordered_map<std::string, std::vector<ImagePointDef>> imagePointsByAsset_;

    bool parseProjectJson(const std::string& path, ProjectDoc& out);
    bool parseGameJson(const std::string& path,    ProjectDoc& out);
    bool extractZip(const std::string& zipPath, const std::string& destDir);
    void loadManifestForRoot(const std::string& rootPath);
};

} // namespace ArtCade::Modules
