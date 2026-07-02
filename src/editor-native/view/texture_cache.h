#pragma once

#include "core/types.h"
#include "editor-native/model/scene_frame_snapshot.h"

#include <filesystem>
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
};

class TextureCache {
public:
    ~TextureCache();

    void prepare(const std::vector<SceneFrameSprite>& sprites,
                 const std::unordered_map<AssetId, TextureRequest>& requests);
    const TextureResource* find(const AssetId& assetId) const;
    void invalidate(const AssetId& assetId);
    void clear();

private:
    const TextureResource* findOrLoad(const TextureRequest& request);

    std::unordered_map<AssetId, TextureResource> entries_;
};

} // namespace ArtCade::EditorNative
