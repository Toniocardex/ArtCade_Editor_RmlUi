#include "../include/renderer.h"
#include "renderer_impl.h"

#include <cmath>

namespace ArtCade::Modules {

void Renderer::setCameraPosition(const Vec2& pos) {
    const Vec2 clamped = renderer_clamp_camera_target(
        impl_->scene_logical_viewport(),
        impl_->scene_world_bounds(),
        impl_->cameraZoom,
        pos);
    impl_->storedGameCamera_.positionX = static_cast<double>(clamped.x);
    impl_->storedGameCamera_.positionY = static_cast<double>(clamped.y);
}

void Renderer::setCameraCenter(const Vec2& center) {
    const Vec2 visible = visibleWorldSize();
    setCameraPosition({
        center.x - visible.x * 0.5f,
        center.y - visible.y * 0.5f,
    });
}

void Renderer::setCameraZoom(float zoom) {
    const float safeZoom = (zoom > 0.f) ? zoom : 0.01f;
    impl_->cameraZoom = safeZoom;
    impl_->storedGameCamera_.zoom = static_cast<double>(safeZoom);
}

void Renderer::resizePlayFramebuffer(float cssW, float cssH, float devicePixelRatio) {
    const float safeDpr = devicePixelRatio > 0.f ? devicePixelRatio : 1.f;
    const uint32_t fbW = static_cast<uint32_t>(std::max(
        1., std::round(static_cast<double>(cssW) * static_cast<double>(safeDpr))));
    const uint32_t fbH = static_cast<uint32_t>(std::max(
        1., std::round(static_cast<double>(cssH) * static_cast<double>(safeDpr))));
    if (fbW != impl_->surface.width() || fbH != impl_->surface.height())
        setWindowSize(fbW, fbH, impl_->surface.title());
}

void Renderer::panCameraByScreenDelta(float dx, float dy) {
    const float zoom = (impl_->camera.zoom > 0.f) ? impl_->camera.zoom : 1.f;
    setCameraPosition({
        impl_->camera.target.x - dx / zoom,
        impl_->camera.target.y - dy / zoom,
    });
}

Vec2 Renderer::visibleWorldSize() const {
    const float zoom = impl_->storedGameCamera_.zoom > 0.
        ? static_cast<float>(impl_->storedGameCamera_.zoom)
        : 1.f;
    return {
        impl_->scene_logical_viewport().x / zoom,
        impl_->scene_logical_viewport().y / zoom,
    };
}

Vec2 Renderer::getCameraPosition() const {
    return {
        static_cast<float>(impl_->storedGameCamera_.positionX),
        static_cast<float>(impl_->storedGameCamera_.positionY),
    };
}

Vec2 Renderer::getCameraCenter() const {
    const Vec2 position = getCameraPosition();
    const Vec2 visible = visibleWorldSize();
    return {
        position.x + visible.x * 0.5f,
        position.y + visible.y * 0.5f,
    };
}

float Renderer::getCameraZoom() const {
    return static_cast<float>(impl_->storedGameCamera_.zoom);
}

} // namespace ArtCade::Modules
