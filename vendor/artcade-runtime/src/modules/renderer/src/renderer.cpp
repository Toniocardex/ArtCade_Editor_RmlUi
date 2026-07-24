#include "../include/renderer.h"
#include "renderer_impl.h"

#include <algorithm>

namespace ArtCade::Modules {

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {
    impl_->camera.zoom = 1.f;
}

Renderer::~Renderer() {
    if (impl_->surface.is_open()) shutdown();
}

bool Renderer::init() {
    const Vec2 logical = impl_->scene_logical_viewport();
    if (!impl_->surface.open(
            impl_->surface.width(),
            impl_->surface.height(),
            impl_->surface.title(),
            static_cast<uint32_t>(logical.x),
            static_cast<uint32_t>(logical.y))) {
        return false;
    }

    impl_->camera.offset = { 0.f, 0.f };
    impl_->camera.target = { 0.f, 0.f };
    impl_->cameraZoom    = 1.f;
    impl_->spriteOutline.load();
    return true;
}

void Renderer::shutdown() {
    if (!impl_->surface.is_open()) return;
    impl_->resources.shutdown();
    impl_->capture.shutdown();
    impl_->spriteOutline.unload();
    impl_->surface.close();
}

void Renderer::setWindowSize(uint32_t w, uint32_t h, const std::string& title) {
    impl_->surface.set_size(w, h, title);
}

void Renderer::setWindowSizeForLogicalViewport(uint32_t logicalWidth,
                                               uint32_t logicalHeight,
                                               const std::string& title) {
    impl_->surface.set_size_for_logical_viewport(logicalWidth, logicalHeight, title);
}

uint32_t Renderer::windowWidth() const { return impl_->surface.width(); }
uint32_t Renderer::windowHeight() const { return impl_->surface.height(); }

void Renderer::setGameViewCompositorEnabled(bool enabled) {
    impl_->gameViewCompositorEnabled = enabled;
    if (!enabled) impl_->resources.release_game_view_target();
}

void Renderer::setOutputPolicy(OutputPolicy policy) {
    impl_->outputPolicy = policy;
}

OutputPolicy Renderer::outputPolicy() const {
    return impl_->outputPolicy;
}

CompositorLayout Renderer::compositorLayout() const {
    return impl_->lastCommittedPresentation_.placement;
}

ScreenClipRect Renderer::worldScreenClipRect() const {
    const Vec2 logical = impl_->scene_logical_viewport();
    const uint32_t fbW = impl_->gameViewCompositorEnabled
        ? std::max(1u, static_cast<uint32_t>(logical.x))
        : impl_->surface.width();
    const uint32_t fbH = impl_->gameViewCompositorEnabled
        ? std::max(1u, static_cast<uint32_t>(logical.y))
        : impl_->surface.height();
    Camera2D cam = impl_->gameViewCompositorEnabled
        ? impl_->gameViewCamera
        : impl_->camera;
    return renderer_compute_world_clip(
        cam, impl_->scene_world_bounds(), fbW, fbH);
}

bool Renderer::shouldClose() const {
    return impl_->surface.should_close();
}

float Renderer::deltaTime() const {
    return impl_->surface.delta_time();
}

ArtCade::Presentation::PresentationMode Renderer::toggleBorderlessFullscreen() {
#ifndef __EMSCRIPTEN__
    if (!impl_->surface.is_open())
        return ArtCade::Presentation::PresentationMode::PlayEmbedded;
    if (impl_->surface.toggle_borderless_fullscreen())
        return ArtCade::Presentation::PresentationMode::PlayFullscreen;
    if (impl_->gameViewCompositorEnabled)
        return ArtCade::Presentation::PresentationMode::PlayEmbedded;
#endif
    return ArtCade::Presentation::PresentationMode::PlayEmbedded;
}

} // namespace ArtCade::Modules
