#include "editor-native/model/tileset_empty_scan.h"

#include <algorithm>

namespace ArtCade::EditorNative {

std::vector<bool> computeEmptyTileFlags(const std::uint8_t* rgba,
                                        int imageWidth, int imageHeight,
                                        const std::vector<TileDefinition>& tiles) {
    std::vector<bool> flags(tiles.size(), false);
    if (!rgba || imageWidth <= 0 || imageHeight <= 0) return flags;

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const TileDefinition& tile = tiles[i];
        const int x0 = std::max(tile.x, 0);
        const int y0 = std::max(tile.y, 0);
        const int x1 = std::min(tile.x + tile.width, imageWidth);
        const int y1 = std::min(tile.y + tile.height, imageHeight);
        if (x1 <= x0 || y1 <= y0) {
            flags[i] = true;   // no overlap with the image: nothing to show
            continue;
        }
        bool empty = true;
        for (int y = y0; empty && y < y1; ++y) {
            const std::uint8_t* row = rgba
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(imageWidth)
                   + static_cast<std::size_t>(x0)) * 4u;
            for (int x = x0; x < x1; ++x, row += 4) {
                if (row[3] != 0) { empty = false; break; }
            }
        }
        flags[i] = empty;
    }
    return flags;
}

} // namespace ArtCade::EditorNative
