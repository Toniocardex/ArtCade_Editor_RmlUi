#pragma once

#include "../../presentation/include/presentation_types.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class TextureCache;
class FontCache;

namespace ArtCade::Modules {

/**
 * GPU asset caches and the GameView render target.
 * Owns texture/font lifetime and RT resize policy.
 */
class RenderResources {
public:
    RenderResources();
    ~RenderResources();

    RenderResources(const RenderResources&) = delete;
    RenderResources& operator=(const RenderResources&) = delete;

    void shutdown();

    void set_texture_key_resolver(std::function<std::string(const std::string&)> resolver);
    void set_font_key_resolver(std::function<std::string(const std::string&)> resolver);

    std::string resolve_texture_key(const std::string& ref) const;
    std::string resolve_font_key(const std::string& ref) const;

    uint32_t load_texture(const std::string& filePath);
    bool register_image_from_memory(const std::string& assetId,
                                    const unsigned char* data,
                                    int len,
                                    const std::string& ext);
    void unload_texture(uint32_t handle);
    void invalidate_image_asset(const std::string& assetPath);
    bool is_texture_loaded(const std::string& resolvedKey) const;

    bool register_font_from_memory(const std::string& path,
                                   const unsigned char* data,
                                   int len,
                                   const std::string& ext,
                                   int baseSize = 32);
    void invalidate_font_asset(const std::string& path);
    void evict_cached_assets();

    /** Ensures a viewport-sized GameView RT exists; returns false on GPU failure. */
    bool ensure_game_view_target(uint32_t width, uint32_t height);
    void release_game_view_target();

    bool has_game_view() const;
    uint32_t game_view_width() const;
    uint32_t game_view_height() const;

    /**
     * Blits the GameView RT to the backbuffer using committed placement.
     * @param layout output placement from PresentationSnapshot
     */
    void blit_game_view(const ArtCade::Presentation::OutputPlacement& layout);

    /** Enters/exits GameView RT rendering (used by renderer frame pass). */
    bool begin_game_view_texture_mode();
    void end_game_view_texture_mode();

    /** Draw-path access for renderer draw translation units. */
    TextureCache& texture_cache();
    const TextureCache& texture_cache() const;
    FontCache& font_cache();
    const FontCache& font_cache() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Modules
