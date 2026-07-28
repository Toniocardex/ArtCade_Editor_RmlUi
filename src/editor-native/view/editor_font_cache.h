#pragma once

#include "editor-native/model/scene_frame_snapshot.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

namespace ArtCade::EditorNative {

struct FontResource {
    Font font{};
    std::filesystem::path resolvedSourcePath;
    bool loaded = false;
    std::string error;
};

// ADR-0036: editor-only, Renderer-independent cache for a Text component's
// configured font (TextComponent::fontPath, project-relative). Mirrors
// TextureCache's shape (demand-load + cache, GPU lifetime tied to the
// project), but is keyed by the resolved fontPath string directly rather
// than an AssetId indirection: SceneFrameText already carries fontPath as a
// project-relative path (matching FontAssetDef::sourcePath), so there is no
// asset catalog lookup needed on the render side, only on the Inspector's
// picker side (which reads ProjectDocument.fontAssets directly to list
// choices, not through this cache).
class EditorFontCache {
public:
    ~EditorFontCache();

    // Demand-loads every distinct non-empty fontPath referenced by `texts`
    // this frame, resolved against `assetRoot`. A missing/failed font is
    // cached as an unloaded entry (not retried every frame) so find()
    // returns nullptr cheaply and the caller falls back to CanvasFont.
    void prepare(const std::vector<SceneFrameText>& texts,
                const std::filesystem::path& assetRoot);
    const FontResource* find(const std::string& fontPath) const;
    void clear();

private:
    const FontResource* findOrLoad(const std::string& fontPath,
                                   const std::filesystem::path& resolvedPath);

    std::unordered_map<std::string, FontResource> entries_;
};

} // namespace ArtCade::EditorNative
