#include "editor-native/view/texture_cache.h"

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

const TextureResource* TextureCache::find(const AssetId& assetId) const {
    const auto it = entries_.find(assetId);
    return it == entries_.end() ? nullptr : &it->second;
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

    resource.texture = LoadTexture(path.string().c_str());
    if (resource.texture.id == 0) {
        resource.error = "failed to load image file: " + path.string();
    } else {
        resource.loaded = true;
        SetTextureFilter(resource.texture, TEXTURE_FILTER_POINT);
    }

    const auto [it, _] = entries_.emplace(assetId, std::move(resource));
    return &it->second;
}

} // namespace ArtCade::EditorNative
