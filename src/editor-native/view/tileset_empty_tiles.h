#pragma once

#include "core/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Explicit availability of the empty-tile classification. Consumers must
// never treat "not scanned yet" as "all tiles are full": the palette blocks
// marquee commits while the mask is not Ready, instead of letting a tile flip
// from selectable to unselectable when the scan lands.
enum class TilesetEmptyMaskStatus {
    Unavailable,   // nothing to scan (no path / empty tileset)
    Scanning,      // reserved for a future async scan; the sync cache never returns it
    Ready,         // flags valid and aligned with tileset.tiles
    Failed,        // image could not be decoded; selection proceeds unfiltered
};

struct TilesetEmptyMaskView {
    TilesetEmptyMaskStatus status = TilesetEmptyMaskStatus::Unavailable;
    const std::vector<bool>* flags = nullptr;   // set only when status == Ready
};

// Derived, rebuildable projection of which tiles of a tileset are fully
// transparent, so the Inspector's tile palette can dim them and turn them
// into stamp holes. Authority stays with the TilesetAsset (rects) and the
// image file (pixels); this only caches the CPU alpha scan of that pair.
//
// Single-entry by design: the palette shows one tileset at a time. The entry
// is keyed by a signature of (image asset, resolved path, tile rects), so any
// re-slice, image swap, or project replacement recomputes on the next query —
// including a load failure, which is remembered under the same signature so an
// unreadable file is not re-decoded on every Inspector refresh.
class TilesetEmptyTileCache {
public:
    // The mask with its explicit status. `flags`, when Ready, stays valid
    // until the next maskFor call.
    TilesetEmptyMaskView maskFor(const TilesetAsset& tileset,
                                 const std::filesystem::path& resolvedImagePath);

private:
    std::string       signature_;
    bool              scanOk_ = false;
    std::vector<bool> flags_;
};

} // namespace ArtCade::EditorNative
