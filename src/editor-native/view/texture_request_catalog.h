#pragma once

#include "editor-native/view/texture_cache.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace ArtCade::EditorNative {

class ProjectDocument;
struct PlayAssetCatalogSnapshot;

// Cached read-only projection from authoring/runtime image assets to resolved
// texture requests. Rebuilds only when the authoritative document revision,
// Play snapshot, or asset root changes.
class TextureRequestCatalog {
public:
    using Requests = std::unordered_map<AssetId, TextureRequest>;

    const Requests& forDocument(const ProjectDocument& document,
                                const std::filesystem::path& assetRoot);
    const Requests& forPlay(const PlayAssetCatalogSnapshot& snapshot,
                            const std::filesystem::path& assetRoot);

private:
    enum class Source { None, Document, Play };

    Source                          source_ = Source::None;
    std::uint64_t                   documentRevision_ = 0;
    const PlayAssetCatalogSnapshot* playSnapshot_ = nullptr;
    std::filesystem::path           assetRoot_;
    Requests                        requests_;
};

} // namespace ArtCade::EditorNative
