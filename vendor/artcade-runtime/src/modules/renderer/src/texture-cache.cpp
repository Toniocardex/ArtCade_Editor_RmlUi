#include "texture-cache.h"

uint32_t TextureCache::load(const std::string& path) {
    auto it = pathToHandle_.find(path);
    if (it != pathToHandle_.end()) return it->second;

    uint32_t h = next_++;
    Entry e;
    e.path = path;

    if (FileExists(path.c_str())) {
        e.tex = LoadTexture(path.c_str());
    }
    if (e.tex.id == 0) {
        // Placeholder: 1×1 magenta
        Image img = GenImageColor(1, 1, MAGENTA);
        e.tex = LoadTextureFromImage(img);
        UnloadImage(img);
    }

    pathToHandle_[path] = h;
    byHandle_[h] = std::move(e);
    return h;
}

uint32_t TextureCache::registerFromMemory(const std::string& path,
                                          const unsigned char* data, int len,
                                          const std::string& ext) {
    if (!data || len <= 0) return 0;

    Image img = LoadImageFromMemory(ext.c_str(), data, len);
    if (img.data == nullptr || img.width <= 0 || img.height <= 0) {
        if (img.data) UnloadImage(img);
        return 0;
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tex.id == 0) return 0;

    // Replace any existing entry for this path (re-upload / hot-swap).
    auto existing = pathToHandle_.find(path);
    if (existing != pathToHandle_.end()) {
        auto bh = byHandle_.find(existing->second);
        if (bh != byHandle_.end()) {
            UnloadTexture(bh->second.tex);
            bh->second.tex  = tex;
            bh->second.path = path;
            return existing->second;
        }
    }

    uint32_t h = next_++;
    Entry e;
    e.path = path;
    e.tex  = tex;
    pathToHandle_[path] = h;
    byHandle_[h] = std::move(e);
    return h;
}

void TextureCache::unload(uint32_t handle) {
    auto it = byHandle_.find(handle);
    if (it == byHandle_.end()) return;
    UnloadTexture(it->second.tex);
    pathToHandle_.erase(it->second.path);
    byHandle_.erase(it);
}

void TextureCache::unloadByPath(const std::string& path) {
    auto it = pathToHandle_.find(path);
    if (it != pathToHandle_.end()) unload(it->second);
}

void TextureCache::unloadAll() {
    for (auto& [h, e] : byHandle_) UnloadTexture(e.tex);
    byHandle_.clear();
    pathToHandle_.clear();
}

const Texture2D* TextureCache::get(uint32_t handle) const {
    auto it = byHandle_.find(handle);
    return (it != byHandle_.end()) ? &it->second.tex : nullptr;
}

const Texture2D* TextureCache::getByPath(const std::string& path) const {
    auto it = pathToHandle_.find(path);
    return (it != pathToHandle_.end()) ? get(it->second) : nullptr;
}

bool TextureCache::isLoaded(const std::string& path) const {
    return pathToHandle_.count(path) > 0;
}

uint32_t TextureCache::handleOf(const std::string& path) const {
    auto it = pathToHandle_.find(path);
    return (it != pathToHandle_.end()) ? it->second : 0u;
}
