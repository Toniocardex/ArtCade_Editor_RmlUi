#pragma once

#include "core/types.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_opaque_geometry.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

namespace ArtCade::EditorNative {

struct TextureRequest {
    AssetId assetId;
    std::filesystem::path resolvedSourcePath;
};

struct TextureResource {
    Texture2D texture{};
    std::filesystem::path resolvedSourcePath;
    bool loaded = false;
    std::string error;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<std::uint8_t> alpha;
    std::map<std::array<int, 4>, std::optional<SpritePixelRect>> opaqueBounds;
};

struct SpriteOpaqueBounds {
    SpritePixelRect source;
    SpritePixelRect opaque;
};

class TextureCache {
public:
    ~TextureCache();

    void prepare(const std::vector<SceneFrameSprite>& sprites,
                 const std::unordered_map<AssetId, TextureRequest>& requests);
    // Also demand-loads each tilemap's underlying tileset image, which may
    // not otherwise appear in `sprites` at all.
    void prepare(const std::vector<SceneFrameSprite>& sprites,
                 const std::vector<SceneFrameTilemap>& tilemaps,
                 const std::unordered_map<AssetId, TextureRequest>& requests);
    const TextureResource* find(const AssetId& assetId) const;
    // Derived from the decoded source image and cached per source rectangle.
    // nullopt means unavailable or fully transparent; callers keep frame bounds.
    std::optional<SpriteOpaqueBounds> opaqueBoundsFor(const SceneFrameSprite& sprite);
    void invalidate(const AssetId& assetId);
    void clear();

private:
    const TextureResource* findOrLoad(const TextureRequest& request);

    std::unordered_map<AssetId, TextureResource> entries_;
};

} // namespace ArtCade::EditorNative
