#pragma once

#include "core/types.h"

#include <cstdint>
#include <vector>

namespace ArtCade::EditorNative {

// Flags each tile whose source rect contains no visible pixel (alpha == 0
// everywhere, after clipping the rect to the image; a rect fully outside the
// image is empty by definition). `rgba` is the sheet's pixel data, row-major,
// 4 bytes per pixel. Returns one flag per tile, aligned with `tiles`.
//
// Pure pixel math over an already-decoded buffer: decoding the image and
// caching the result belong to the caller (see view/tileset_empty_tiles.h).
std::vector<bool> computeEmptyTileFlags(const std::uint8_t* rgba,
                                        int imageWidth, int imageHeight,
                                        const std::vector<TileDefinition>& tiles);

} // namespace ArtCade::EditorNative
