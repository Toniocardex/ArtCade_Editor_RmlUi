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

const std::vector<bool>* TilesetEmptyTileCache::flagsFor(
    const TilesetAsset& tileset, const std::filesystem::path& resolvedImagePath) {
    if (resolvedImagePath.empty() || tileset.tiles.empty()) return nullptr;

    std::string signature = scanSignature(tileset, resolvedImagePath);
    if (signature == signature_) return scanOk_ ? &flags_ : nullptr;

    signature_ = std::move(signature);
    scanOk_ = false;
    flags_.clear();

    Image image = LoadImage(resolvedImagePath.string().c_str());
    if (image.data == nullptr) return nullptr;
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (image.data == nullptr) { UnloadImage(image); return nullptr; }

    flags_ = computeEmptyTileFlags(static_cast<const std::uint8_t*>(image.data),
                                   image.width, image.height, tileset.tiles);
    UnloadImage(image);
    scanOk_ = true;
    return &flags_;
}

} // namespace ArtCade::EditorNative
