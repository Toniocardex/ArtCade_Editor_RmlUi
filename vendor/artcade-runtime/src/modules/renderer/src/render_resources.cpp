#include "../include/render_resources.h"

#include "font-cache.h"
#include "texture-cache.h"

#include <raylib.h>

#include <algorithm>
#include <utility>

namespace ArtCade::Modules {

struct RenderResources::Impl {
    TextureCache tex_cache;
    FontCache font_cache;
    std::function<std::string(const std::string&)> texture_key_resolver;
    std::function<std::string(const std::string&)> font_key_resolver;

    struct GameViewTarget {
        RenderTexture2D rt{};
        uint32_t w = 0;
        uint32_t h = 0;
    } game_view;
};

RenderResources::RenderResources() : impl_(std::make_unique<Impl>()) {}

RenderResources::~RenderResources() {
    shutdown();
}

void RenderResources::shutdown() {
    release_game_view_target();
    impl_->tex_cache.unloadAll();
    impl_->font_cache.unloadAll();
}

void RenderResources::set_texture_key_resolver(
    std::function<std::string(const std::string&)> resolver) {
    impl_->texture_key_resolver = std::move(resolver);
}

void RenderResources::set_font_key_resolver(
    std::function<std::string(const std::string&)> resolver) {
    impl_->font_key_resolver = std::move(resolver);
}

std::string RenderResources::resolve_texture_key(const std::string& ref) const {
    if (ref.empty()) return ref;
    if (impl_->texture_key_resolver) return impl_->texture_key_resolver(ref);
    return ref;
}

std::string RenderResources::resolve_font_key(const std::string& ref) const {
    if (ref.empty()) return ref;
    if (impl_->font_key_resolver) return impl_->font_key_resolver(ref);
    return ref;
}

uint32_t RenderResources::load_texture(const std::string& filePath) {
    return impl_->tex_cache.load(filePath);
}

bool RenderResources::register_image_from_memory(const std::string& assetId,
                                                 const unsigned char* data,
                                                 int len,
                                                 const std::string& ext) {
    return impl_->tex_cache.registerFromMemory(assetId, data, len, ext) != 0;
}

void RenderResources::unload_texture(uint32_t handle) {
    impl_->tex_cache.unload(handle);
}

void RenderResources::invalidate_image_asset(const std::string& assetPath) {
    impl_->tex_cache.unloadByPath(assetPath);
}

bool RenderResources::is_texture_loaded(const std::string& resolvedKey) const {
    return impl_->tex_cache.isLoaded(resolvedKey);
}

bool RenderResources::register_font_from_memory(const std::string& path,
                                                const unsigned char* data,
                                                int len,
                                                const std::string& ext,
                                                int baseSize) {
    return impl_->font_cache.registerFromMemory(path, data, len, ext, baseSize);
}

void RenderResources::invalidate_font_asset(const std::string& path) {
    impl_->font_cache.invalidate(path);
}

void RenderResources::evict_cached_assets() {
    impl_->tex_cache.unloadAll();
    impl_->font_cache.unloadAll();
}

bool RenderResources::ensure_game_view_target(uint32_t width, uint32_t height) {
    const uint32_t safeW = std::max(1u, width);
    const uint32_t safeH = std::max(1u, height);
    if (impl_->game_view.rt.id != 0
        && impl_->game_view.w == safeW
        && impl_->game_view.h == safeH) {
        return true;
    }
    release_game_view_target();
    impl_->game_view.rt = LoadRenderTexture(static_cast<int>(safeW),
                                             static_cast<int>(safeH));
    if (impl_->game_view.rt.id == 0) return false;
    impl_->game_view.w = safeW;
    impl_->game_view.h = safeH;
    return true;
}

void RenderResources::release_game_view_target() {
    if (impl_->game_view.rt.id == 0) return;
    UnloadRenderTexture(impl_->game_view.rt);
    impl_->game_view.rt = {};
    impl_->game_view.w = 0;
    impl_->game_view.h = 0;
}

bool RenderResources::has_game_view() const {
    return impl_->game_view.rt.id != 0;
}

uint32_t RenderResources::game_view_width() const {
    return impl_->game_view.w;
}

uint32_t RenderResources::game_view_height() const {
    return impl_->game_view.h;
}

void RenderResources::blit_game_view(
    const ArtCade::Presentation::OutputPlacement& layout) {
    if (impl_->game_view.rt.id == 0) return;
    const float srcW = layout.srcW > 0.
        ? static_cast<float>(layout.srcW)
        : static_cast<float>(impl_->game_view.w);
    const float srcH = layout.srcH > 0.
        ? static_cast<float>(layout.srcH)
        : static_cast<float>(impl_->game_view.h);
    const float srcX = static_cast<float>(layout.srcX);
    const float srcY = static_cast<float>(layout.srcY);
    DrawTexturePro(
        impl_->game_view.rt.texture,
        Rectangle{ srcX, srcY, srcW, -srcH },
        Rectangle{
            static_cast<float>(layout.destX),
            static_cast<float>(layout.destY),
            static_cast<float>(layout.destW),
            static_cast<float>(layout.destH),
        },
        Vector2{ 0.f, 0.f },
        0.f,
        WHITE);
}

bool RenderResources::begin_game_view_texture_mode() {
    if (impl_->game_view.rt.id == 0) return false;
    BeginTextureMode(impl_->game_view.rt);
    return true;
}

void RenderResources::end_game_view_texture_mode() {
    EndTextureMode();
}

TextureCache& RenderResources::texture_cache() {
    return impl_->tex_cache;
}

const TextureCache& RenderResources::texture_cache() const {
    return impl_->tex_cache;
}

FontCache& RenderResources::font_cache() {
    return impl_->font_cache;
}

const FontCache& RenderResources::font_cache() const {
    return impl_->font_cache;
}

} // namespace ArtCade::Modules
