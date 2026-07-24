#include "../include/texture-manager.h"
#include <raylib.h>
#include <string>
#include <unordered_map>

namespace ArtCade::Modules {

// ------------------------------------------------------------------ Pimpl

struct TextureManager::Impl {
    struct Entry {
        uint32_t    handle   = 0;
        std::string path;
        Texture2D   texture  = {};
        int         refCount = 0;
    };

    std::unordered_map<uint32_t, Entry>       byHandle;
    std::unordered_map<std::string, uint32_t> pathToHandle;
    uint32_t nextHandle = 1;

    static Texture2D makePlaceholder() {
        Image img = GenImageColor(1, 1, MAGENTA);
        Texture2D tex = LoadTextureFromImage(img);
        UnloadImage(img);
        return tex;
    }
};

// ------------------------------------------------------------------ lifecycle

TextureManager::TextureManager() : impl_(new Impl{}) {}

TextureManager::~TextureManager() {
    unloadAll();
    delete impl_;
}

bool TextureManager::init()     { return true; }
void TextureManager::shutdown() { unloadAll(); }

// ------------------------------------------------------------------ load / release

uint32_t TextureManager::load(const std::string& path) {
    auto pit = impl_->pathToHandle.find(path);
    if (pit != impl_->pathToHandle.end()) {
        impl_->byHandle[pit->second].refCount++;
        return pit->second;
    }

    uint32_t h = impl_->nextHandle++;
    Impl::Entry entry;
    entry.handle   = h;
    entry.path     = path;
    entry.refCount = 1;

    if (FileExists(path.c_str())) {
        entry.texture = LoadTexture(path.c_str());
        if (entry.texture.id == 0)
            entry.texture = Impl::makePlaceholder();
    } else {
        entry.texture = Impl::makePlaceholder();
    }

    impl_->pathToHandle[path] = h;
    impl_->byHandle[h]        = std::move(entry);
    return h;
}

void TextureManager::release(uint32_t handle) {
    auto it = impl_->byHandle.find(handle);
    if (it == impl_->byHandle.end()) return;

    if (--it->second.refCount <= 0) {
        UnloadTexture(it->second.texture);
        impl_->pathToHandle.erase(it->second.path);
        impl_->byHandle.erase(it);
    }
}

void TextureManager::release(const std::string& path) {
    auto it = impl_->pathToHandle.find(path);
    if (it != impl_->pathToHandle.end())
        release(it->second);
}

// ------------------------------------------------------------------ access

bool TextureManager::getInfo(uint32_t handle, TextureInfo& out) const {
    auto it = impl_->byHandle.find(handle);
    if (it == impl_->byHandle.end()) return false;
    const Texture2D& t = it->second.texture;
    out.gpuId  = t.id;
    out.width  = t.width;
    out.height = t.height;
    return true;
}

uint32_t TextureManager::handleOf(const std::string& path) const {
    auto it = impl_->pathToHandle.find(path);
    return (it != impl_->pathToHandle.end()) ? it->second : 0u;
}

std::size_t TextureManager::loadedCount() const {
    return impl_->byHandle.size();
}

void TextureManager::unloadAll() {
    for (auto& [h, entry] : impl_->byHandle)
        UnloadTexture(entry.texture);
    impl_->byHandle.clear();
    impl_->pathToHandle.clear();
}

} // namespace ArtCade::Modules
