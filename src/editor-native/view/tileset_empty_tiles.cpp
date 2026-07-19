#include "editor-native/view/tileset_empty_tiles.h"

#include "editor-native/model/tileset_empty_scan.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

namespace {

std::string scanSignature(const TilesetAsset& tileset,
                          const std::filesystem::path& resolvedImagePath) {
    std::string signature = tileset.imageAssetId;
    signature += '|';
    signature += resolvedImagePath.string();
    for (const TileDefinition& tile : tileset.tiles) {
        signature += '|';
        signature += std::to_string(tile.x) + ',' + std::to_string(tile.y) + ','
                   + std::to_string(tile.width) + ',' + std::to_string(tile.height);
    }
    return signature;
}

} // namespace

TilesetEmptyMaskView TilesetEmptyTileCache::maskFor(
    const TilesetAsset& tileset, const std::filesystem::path& resolvedImagePath) {
    if (resolvedImagePath.empty() || tileset.tiles.empty()) {
        return {TilesetEmptyMaskStatus::Unavailable, nullptr};
    }

    std::string signature = scanSignature(tileset, resolvedImagePath);
    if (signature != signature_) {
        signature_ = std::move(signature);
        scanOk_ = false;
        flags_.clear();

        Image image = LoadImage(resolvedImagePath.string().c_str());
        if (image.data != nullptr) {
            ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            if (image.data != nullptr) {
                flags_ = computeEmptyTileFlags(static_cast<const std::uint8_t*>(image.data),
                                               image.width, image.height, tileset.tiles);
                scanOk_ = true;
            }
            UnloadImage(image);
        }
    }
    // A misaligned mask must never be indexed; report it as a failure rather
    // than Ready-with-wrong-flags (defensive - the signature covers the rects,
    // so this only trips if the tile COUNT changed without changing any rect).
    if (scanOk_ && flags_.size() != tileset.tiles.size()) {
        return {TilesetEmptyMaskStatus::Failed, nullptr};
    }
    return scanOk_ ? TilesetEmptyMaskView{TilesetEmptyMaskStatus::Ready, &flags_}
                   : TilesetEmptyMaskView{TilesetEmptyMaskStatus::Failed, nullptr};
}

} // namespace ArtCade::EditorNative
