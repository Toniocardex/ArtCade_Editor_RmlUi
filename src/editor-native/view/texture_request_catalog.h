#pragma once

#include "editor-native/view/texture_cache.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace ArtCade::EditorNative {

class ProjectDocument;

// Cached read-only projection from authoring image assets to resolved
// texture requests. Rebuilds only when the authoritative document revision
// or asset root changes. Play draws the same image assets as Edit (no
// unsaved-snapshot divergence like Scripts have), so there is no separate
// Play-scoped source.
class TextureRequestCatalog {
public:
    using Requests = std::unordered_map<AssetId, TextureRequest>;

    const Requests& forDocument(const ProjectDocument& document,
                                const std::filesystem::path& assetRoot);

private:
    bool                   hasRequests_ = false;
    std::uint64_t          documentRevision_ = 0;
    std::filesystem::path  assetRoot_;
    Requests               requests_;
};

} // namespace ArtCade::EditorNative
