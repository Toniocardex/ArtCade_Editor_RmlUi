#include "editor-native/view/editor_font_cache.h"

#include "editor-native/model/path_confinement.h"

#include <raylib.h>

namespace ArtCade::EditorNative {
namespace {

std::filesystem::path resolveFontPath(const std::filesystem::path& assetRoot,
                                      const std::string& fontPath) {
    if (fontPath.empty()) return {};
    const PathConfinementResult resolved = resolvePathInsideRoot(
        assetRoot, std::filesystem::u8path(fontPath));
    return resolved.ok ? resolved.value : std::filesystem::path{};
}

} // namespace

EditorFontCache::~EditorFontCache() {
    clear();
}

void EditorFontCache::prepare(const std::vector<SceneFrameText>& texts,
                              const std::filesystem::path& assetRoot) {
    for (const SceneFrameText& text : texts) {
        if (text.fontPath.empty()) continue;
        (void)findOrLoad(text.fontPath, resolveFontPath(assetRoot, text.fontPath));
    }
}

const FontResource* EditorFontCache::find(const std::string& fontPath) const {
    if (fontPath.empty()) return nullptr;
    const auto it = entries_.find(fontPath);
    return it == entries_.end() ? nullptr : &it->second;
}

void EditorFontCache::clear() {
    if (!IsWindowReady()) {
        entries_.clear();
        return;
    }
    for (auto& [_, resource] : entries_) {
        if (resource.loaded && resource.font.texture.id != 0) {
            UnloadFont(resource.font);
            resource.font = Font{};
        }
    }
    entries_.clear();
}

const FontResource* EditorFontCache::findOrLoad(const std::string& fontPath,
                                                const std::filesystem::path& resolvedPath) {
    const auto existing = entries_.find(fontPath);
    if (existing != entries_.end()) {
        if (existing->second.resolvedSourcePath == resolvedPath) {
            return &existing->second;
        }
        if (existing->second.loaded && existing->second.font.texture.id != 0) {
            UnloadFont(existing->second.font);
        }
        entries_.erase(existing);
    }

    FontResource resource;
    resource.resolvedSourcePath = resolvedPath;
    if (resolvedPath.empty()) {
        resource.error = "font path escapes the project asset root";
        const auto [it, _] = entries_.emplace(fontPath, std::move(resource));
        return &it->second;
    }
    if (!std::filesystem::exists(resolvedPath)) {
        resource.error = "missing font file: " + resolvedPath.string();
        const auto [it, _] = entries_.emplace(fontPath, std::move(resource));
        return &it->second;
    }

    resource.font = LoadFontEx(resolvedPath.string().c_str(), 32, nullptr, 0);
    if (resource.font.texture.id == 0) {
        resource.error = "failed to load font file: " + resolvedPath.string();
    } else {
        resource.loaded = true;
        SetTextureFilter(resource.font.texture, TEXTURE_FILTER_BILINEAR);
    }

    const auto [it, _] = entries_.emplace(fontPath, std::move(resource));
    return &it->second;
}

} // namespace ArtCade::EditorNative
