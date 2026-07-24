#include "font-cache.h"
#include <algorithm>

namespace {

int clampFontSize(int size) {
    return std::max(8, std::min(size, 128));
}

} // namespace

bool FontCache::registerFromMemory(const std::string& path,
                                   const unsigned char* data, int len,
                                   const std::string& ext,
                                   int baseSize) {
    if (path.empty() || !data || len <= 0) return false;
    invalidate(path);
    const int fontSize = clampFontSize(baseSize);
    const char* hint = ext.empty() ? ".ttf" : ext.c_str();
    Font font = LoadFontFromMemory(hint, data, len, fontSize, nullptr, 0);
    if (font.texture.id == 0) return false;
    byPath_[path] = Entry{ font, true };
    return true;
}

void FontCache::invalidate(const std::string& path) {
    auto it = byPath_.find(path);
    if (it == byPath_.end()) return;
    if (it->second.loaded && it->second.font.texture.id != 0)
        UnloadFont(it->second.font);
    byPath_.erase(it);
}

void FontCache::unloadAll() {
    for (auto& [path, entry] : byPath_) {
        (void)path;
        if (entry.loaded && entry.font.texture.id != 0)
            UnloadFont(entry.font);
    }
    byPath_.clear();
}

const Font* FontCache::get(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = byPath_.find(path);
    if (it == byPath_.end()) {
        if (!FileExists(path.c_str())) return nullptr;
        Font font = LoadFont(path.c_str());
        if (font.texture.id == 0) return nullptr;
        byPath_[path] = Entry{ font, true };
        it = byPath_.find(path);
    }
    if (!it->second.loaded || it->second.font.texture.id == 0)
        return nullptr;
    return &it->second.font;
}
