#include "editor-native/view/texture_cache.h"

#include <algorithm>
#include <cmath>

#include <raylib.h>

namespace ArtCade::EditorNative {

TextureCache::~TextureCache() {
    clear();
}

void TextureCache::prepare(const std::vector<SceneFrameSprite>& sprites,
                           const std::unordered_map<AssetId, TextureRequest>& requests) {
    for (const SceneFrameSprite& sprite : sprites) {
        if (!sprite.visible || sprite.assetId.empty()) continue;
        const auto requestIt = requests.find(sprite.assetId);
        if (requestIt == requests.end()) continue;
        (void)findOrLoad(requestIt->second);
    }
}

void TextureCache::prepare(const std::vector<SceneFrameSprite>& sprites,
                           const std::vector<SceneFrameTilemap>& tilemaps,
                           const std::unordered_map<AssetId, TextureRequest>& requests) {
    prepare(sprites, requests);
    for (const SceneFrameTilemap& tilemap : tilemaps) {
        if (tilemap.imageAssetId.empty()) continue;
        const auto requestIt = requests.find(tilemap.imageAssetId);
        if (requestIt == requests.end()) continue;
        (void)findOrLoad(requestIt->second);
    }
}

const TextureResource* TextureCache::find(const AssetId& assetId) const {
    const auto it = entries_.find(assetId);
    return it == entries_.end() ? nullptr : &it->second;
}

std::optional<SpriteOpaqueBounds> TextureCache::opaqueBoundsFor(
    const SceneFrameSprite& sprite) {
    auto resourceIt = entries_.find(sprite.assetId);
    if (resourceIt == entries_.end()) return std::nullopt;
    TextureResource& resource = resourceIt->second;
    if (!resource.loaded || resource.alpha.empty()
        || resource.imageWidth <= 0 || resource.imageHeight <= 0) {
        return std::nullopt;
    }

    SpritePixelRect source{0, 0, resource.imageWidth, resource.imageHeight};
    if (sprite.hasSource && sprite.source.width > 0.f && sprite.source.height > 0.f) {
        const int x0 = static_cast<int>(std::floor(sprite.source.x));
        const int y0 = static_cast<int>(std::floor(sprite.source.y));
        const int x1 = static_cast<int>(std::ceil(sprite.source.x + sprite.source.width));
        const int y1 = static_cast<int>(std::ceil(sprite.source.y + sprite.source.height));
        source = SpritePixelRect{x0, y0, x1 - x0, y1 - y0};
    }
    const std::array<int, 4> key{source.x, source.y, source.width, source.height};
    auto cached = resource.opaqueBounds.find(key);
    if (cached == resource.opaqueBounds.end()) {
        cached = resource.opaqueBounds.emplace(
            key,
            computeOpaqueSpritePixelBounds(
                resource.alpha.data(), resource.imageWidth, resource.imageHeight, source))
                     .first;
    }
    if (!cached->second) return std::nullopt;
    return SpriteOpaqueBounds{source, *cached->second};
}

void TextureCache::invalidate(const AssetId& assetId) {
    const auto it = entries_.find(assetId);
    if (it == entries_.end()) return;
    if (IsWindowReady() && it->second.loaded && it->second.texture.id != 0) {
        UnloadTexture(it->second.texture);
    }
    entries_.erase(it);
}

void TextureCache::clear() {
    if (!IsWindowReady()) {
        entries_.clear();
        return;
    }
    for (auto& [_, resource] : entries_) {
        if (resource.loaded && resource.texture.id != 0) {
            UnloadTexture(resource.texture);
            resource.texture = Texture2D{};
        }
    }
    entries_.clear();
}

const TextureResource* TextureCache::findOrLoad(const TextureRequest& request) {
    const AssetId& assetId = request.assetId;
    const auto existing = entries_.find(assetId);
    if (existing != entries_.end()) {
        if (existing->second.resolvedSourcePath == request.resolvedSourcePath) {
            return &existing->second;
        }
        invalidate(assetId);
    }

    TextureResource resource;
    resource.resolvedSourcePath = request.resolvedSourcePath;
    if (request.resolvedSourcePath.empty()) {
        resource.error = "image asset has no sourcePath";
        const auto [it, _] = entries_.emplace(assetId, std::move(resource));
        return &it->second;
    }

    const std::filesystem::path& path = request.resolvedSourcePath;
    if (!std::filesystem::exists(path)) {
        resource.error = "missing image file: " + path.string();
        const auto [it, _] = entries_.emplace(assetId, std::move(resource));
        return &it->second;
    }

    Image image = LoadImage(path.string().c_str());
    if (!image.data || image.width <= 0 || image.height <= 0) {
        resource.error = "failed to load image file: " + path.string();
    } else {
        resource.imageWidth = image.width;
        resource.imageHeight = image.height;
        if (Color* pixels = LoadImageColors(image)) {
            const std::size_t count = static_cast<std::size_t>(image.width)
                * static_cast<std::size_t>(image.height);
            resource.alpha.resize(count);
            for (std::size_t i = 0; i < count; ++i) resource.alpha[i] = pixels[i].a;
            UnloadImageColors(pixels);
        }
        resource.texture = LoadTextureFromImage(image);
        UnloadImage(image);
        if (resource.texture.id == 0) {
            resource.error = "failed to upload image texture: " + path.string();
        } else {
            resource.loaded = true;
            SetTextureFilter(resource.texture, TEXTURE_FILTER_POINT);
        }
    }

    const auto [it, _] = entries_.emplace(assetId, std::move(resource));
    return &it->second;
}

} // namespace ArtCade::EditorNative
