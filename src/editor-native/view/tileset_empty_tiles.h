#pragma once

#include "core/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Derived, rebuildable projection of which tiles of a tileset are fully
// transparent, so the Inspector's tile palette can hide them. Authority stays
// with the TilesetAsset (rects) and the image file (pixels); this only caches
// the CPU alpha scan of that pair.
//
// Single-entry by design: the palette shows one tileset at a time. The entry
// is keyed by a signature of (image asset, resolved path, tile rects), so any
// re-slice, image swap, or project replacement recomputes on the next query —
// including a load failure, which is remembered under the same signature so an
// unreadable file is not re-decoded on every Inspector refresh.
class TilesetEmptyTileCache {
public:
    // Emptiness flags aligned with tileset.tiles, or nullptr while the image
    // cannot be decoded. The pointer stays valid until the next flagsFor call.
    const std::vector<bool>* flagsFor(const TilesetAsset& tileset,
                                      const std::filesystem::path& resolvedImagePath);

private:
    std::string       signature_;
    bool              scanOk_ = false;
    std::vector<bool> flags_;
};

} // namespace ArtCade::EditorNative
