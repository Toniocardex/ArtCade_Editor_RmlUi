#include "../include/renderer.h"
#include "renderer_impl.h"

#include "../../presentation/include/presentation_mode.h"
#include "../../presentation/include/presentation_state.h"

#include <algorithm>

#ifdef __EMSCRIPTEN__
extern "C" {
#include "rlgl.h"
}
#endif

namespace ArtCade::Modules {

namespace {

#ifdef __EMSCRIPTEN__
/** Flush Raylib's shape batch while scissor + Mode2D are still active (ES2). */
void flush_active_shape_batch() {
    rlDrawRenderBatchActive();
}
#endif

} // namespace

Vec2 Renderer::Impl::scene_world_bounds() const {
    if (committedGeometryActive_) return committedWorldSize_;
    return {
        ProjectDefaults::kSceneWorldWidth,
        ProjectDefaults::kSceneWorldHeight,
    };
}

Vec2 Renderer::Impl::scene_logical_viewport() const {
    if (committedGeometryActive_) return committedLogicalViewport_;
    return {
        ProjectDefaults::kSceneViewportWidth,
        ProjectDefaults::kSceneViewportHeight,
    };
}

namespace {

using ArtCade::Presentation::PresentationMode;
using ArtCade::Presentation::PresentationSimulationInputs;
using ArtCade::Presentation::PresentationStateInputs;
using ArtCade::Presentation::ViewCamera2D;

Camera2D raylib_camera_from_view(const ViewCamera2D& view) {
    const double zoom = view.zoom > 0. ? view.zoom : 1.;
    Camera2D camera{};
    // Raylib 5 Camera2D field order is offset, target, rotation, zoom — never
    // use positional braced init here (target/offset would be swapped).
    camera.offset = {
        static_cast<float>(view.offsetX),
        static_cast<float>(view.offsetY),
    };
    camera.target = {
        static_cast<float>(view.targetX),
        static_cast<float>(view.targetY),
    };
    camera.rotation = 0.f;
    camera.zoom = static_cast<float>(zoom);
    return camera;
}

Camera2D surface_camera_from_snapshot(
    const ArtCade::Presentation::PresentationSnapshot& snapshot) {
    if (snapshot.useIdentityPlacement
        || snapshot.effectiveMode == PresentationMode::SceneEdit) {
        return raylib_camera_from_view(snapshot.pickingCamera);
    }
    const double scale = snapshot.placement.scaleX > 0.
        ? snapshot.placement.scaleX
        : 1.;
    const ViewCamera2D& view = snapshot.pickingCamera;
    const double zoom = view.zoom > 0. ? view.zoom : 1.;
    Camera2D camera{};
    camera.target = {
        static_cast<float>(view.targetX),
        static_cast<float>(view.targetY),
    };
    camera.offset = {
        static_cast<float>(snapshot.placement.destX + view.offsetX * scale),
        static_cast<float>(snapshot.placement.destY + view.offsetY * scale),
    };
    camera.zoom = static_cast<float>(zoom * scale);
    return camera;
}

} // namespace

void Renderer::Impl::apply_projection_from_snapshot(
    const ArtCade::Presentation::PresentationSnapshot& snapshot) {
    compositorLayout = snapshot.placement;
    displayScale = static_cast<float>(snapshot.placement.scaleX);
    viewportDrawSize = {
        static_cast<float>(snapshot.placement.destW),
        static_cast<float>(snapshot.placement.destH),
    };
    viewportOffset = {
        static_cast<float>(snapshot.placement.destX),
        static_cast<float>(snapshot.placement.destY),
    };
}

void Renderer::Impl::apply_frame_presentation(
    const ArtCade::Presentation::PresentationSnapshot& snapshot) {
    apply_projection_from_snapshot(snapshot);
    const Camera2D logicalCamera =
        raylib_camera_from_view(snapshot.pickingCamera);
    const Camera2D surfaceCamera = surface_camera_from_snapshot(snapshot);
    if (gameViewCompositorEnabled) {
        gameViewCamera = logicalCamera;
        camera = surfaceCamera;
    } else {
        camera = surfaceCamera;
        gameViewCamera = surfaceCamera;
    }
    cameraZoom = camera.zoom;
}

Camera2D Renderer::Impl::frame_camera_with_shake() const {
    return inGameViewTexturePass ? gameViewCamera : camera;
}

void Renderer::Impl::begin_world_scissor(const Camera2D& frameCamera) {
    // SceneEdit draws the framed world on the full editor surface. Scissor is
    // for play/camera letterboxing; with OFFSCREEN_FRAMEBUFFER a stale FBO
    // height in rlScissor Y-flip can clip the entire world (clear-only view).
    if (hasCommittedPresentation_
        && lastCommittedPresentation_.effectiveMode
            == ArtCade::Presentation::PresentationMode::SceneEdit) {
        worldScissorActive = false;
        return;
    }
    const uint32_t fbW = framebuffer_width();
    const uint32_t fbH = framebuffer_height();
    const Vec2& bounds = scene_world_bounds();
    const ScreenClipRect clip = renderer_compute_world_clip(
        frameCamera, bounds, fbW, fbH);
    if (clip.width <= 0.f || clip.height <= 0.f) {
        worldScissorActive = false;
        return;
    }
    BeginScissorMode(static_cast<int>(clip.x),
                     static_cast<int>(clip.y),
                     static_cast<int>(clip.width),
                     static_cast<int>(clip.height));
    worldScissorActive = true;
}

void Renderer::Impl::end_world_scissor() {
    if (!worldScissorActive) return;
    EndScissorMode();
    worldScissorActive = false;
}

void Renderer::clearDrawQueue() {
    impl_->drawQueue.clear();
}

void Renderer::setGameCameraModifiers(
    const ArtCade::Presentation::CameraModifiers& modifiers) {
    impl_->gameModifiers_ = modifiers;
}

PresentationSimulationInputs Renderer::gatherSimulationPresentationInputs() const {
    PresentationSimulationInputs sim{};
    sim.outputPolicy = impl_->outputPolicy;
    sim.gameViewCompositorEnabled = impl_->gameViewCompositorEnabled;
    sim.gameCamera = impl_->storedGameCamera_;
    sim.gameModifiers = impl_->gameModifiers_;
    return sim;
}

void Renderer::applyFramePresentation(
    const ArtCade::Presentation::PresentationSnapshot& presentation) {
    impl_->lastCommittedPresentation_ = presentation;
    impl_->hasCommittedPresentation_ = true;
    impl_->apply_frame_presentation(presentation);
}

void Renderer::commitFrameGeometry(const Vec2& worldSize,
                                   const Vec2& logicalViewport) {
    if (worldSize.x <= 0.f || worldSize.y <= 0.f
        || logicalViewport.x <= 0.f || logicalViewport.y <= 0.f) {
        impl_->committedGeometryActive_ = false;
        return;
    }
    impl_->committedWorldSize_ = {
        std::max(1.f, worldSize.x),
        std::max(1.f, worldSize.y),
    };
    impl_->committedLogicalViewport_ = {
        std::max(1.f, logicalViewport.x),
        std::max(1.f, logicalViewport.y),
    };
    impl_->committedGeometryActive_ = true;
    if (impl_->surface.is_open()) {
        impl_->surface.set_min_viewport_size(
            static_cast<uint32_t>(impl_->committedLogicalViewport_.x),
            static_cast<uint32_t>(impl_->committedLogicalViewport_.y));
    }
}

void Renderer::beginFrame(
    const ArtCade::Presentation::PresentationSnapshot& presentation,
    const Vec2& worldSize,
    const Vec2& logicalViewport,
    const Vec4& clearColor) {
    commitFrameGeometry(worldSize, logicalViewport);

    impl_->lastCommittedPresentation_ = presentation;
    impl_->hasCommittedPresentation_ = true;
    impl_->surface.sync_size_from_raylib();
    if (impl_->gameViewCompositorEnabled && impl_->surface.is_open()) {
        const Vec2 logical = impl_->scene_logical_viewport();
        const uint32_t vpW = std::max(1u, static_cast<uint32_t>(logical.x));
        const uint32_t vpH = std::max(1u, static_cast<uint32_t>(logical.y));
        if (!impl_->resources.ensure_game_view_target(vpW, vpH))
            impl_->gameViewCompositorEnabled = false;
    }
    impl_->apply_frame_presentation(presentation);
    impl_->worldScissorActive = false;
    impl_->worldModeActive = false;
    impl_->inGameViewTexturePass = false;
    impl_->surface.begin_drawing();

    // Compositor is play-only. If the flag is stale after mode switches, SceneEdit
    // must still open Mode2D on the backbuffer or only ClearBackground is visible.
    const bool useGameViewCompositor =
        impl_->gameViewCompositorEnabled
        && presentation.effectiveMode != PresentationMode::SceneEdit
        && presentation.effectiveMode != PresentationMode::CameraPreview;

    if (useGameViewCompositor) {
        ClearBackground(renderer_to_color(clearColor));
        return;
    }

    ClearBackground(renderer_to_color(clearColor));
    const Camera2D frameCamera = impl_->frame_camera_with_shake();
    BeginMode2D(frameCamera);
    impl_->worldModeActive = true;
    impl_->begin_world_scissor(frameCamera);
}

void Renderer::beginGameViewPass(const Vec4& clearColor) {
    if (!impl_->gameViewCompositorEnabled
        || !impl_->resources.begin_game_view_texture_mode()) {
        return;
    }
    impl_->inGameViewTexturePass = true;
    ClearBackground(renderer_to_color(clearColor));
    Camera2D frameCamera = impl_->gameViewCamera;
    BeginMode2D(frameCamera);
    impl_->worldModeActive = true;
    impl_->begin_world_scissor(frameCamera);
}

void Renderer::blitGameViewToBackbuffer() {
    if (!impl_->gameViewCompositorEnabled || !impl_->resources.has_game_view())
        return;
    impl_->resources.blit_game_view(impl_->lastCommittedPresentation_.placement);
}

void Renderer::endWorldPass() {
    if (!impl_->worldModeActive) {
        impl_->drawQueue.clear();
        return;
    }
    for (const auto& cmd : impl_->drawQueue) {
        Color c{ cmd.cr, cmd.cg, cmd.cb, cmd.ca };
        switch (cmd.type) {
        case DrawCmd::Type::Rect:
            DrawRectangleV({ cmd.x, cmd.y }, { cmd.x2, cmd.y2 }, c);
            break;
        case DrawCmd::Type::Line:
            DrawLineV({ cmd.x, cmd.y }, { cmd.x2, cmd.y2 }, c);
            break;
        case DrawCmd::Type::Circle:
            DrawCircleV({ cmd.x, cmd.y }, cmd.r, c);
            break;
        case DrawCmd::Type::Text: {
            const std::string fontKey = resolvedFontKey(cmd.fontPath);
            const Font* font = fontKey.empty()
                ? nullptr
                : impl_->resources.font_cache().get(fontKey);
            draw_text_command(cmd, font);
            break;
        }
        case DrawCmd::Type::Image:
            (void)draw_image_command(impl_->resources.texture_cache(),
                                     resolvedTextureKey(cmd.assetId),
                                     cmd);
            break;
        }
    }
#ifdef __EMSCRIPTEN__
    flush_active_shape_batch();
#endif
    impl_->drawQueue.clear();
    impl_->end_world_scissor();
    EndMode2D();
    impl_->worldModeActive = false;
    if (impl_->inGameViewTexturePass) {
        impl_->resources.end_game_view_texture_mode();
        impl_->inGameViewTexturePass = false;
    }
}

void Renderer::endScreenPass() {
    Camera2D screenCamera = {};
    screenCamera.offset = { impl_->viewportOffset.x, impl_->viewportOffset.y };
    screenCamera.target = { 0.f, 0.f };
    screenCamera.zoom = impl_->displayScale;
    BeginMode2D(screenCamera);
    for (const auto& cmd : impl_->screenQueue) {
        Color c{ cmd.cr, cmd.cg, cmd.cb, cmd.ca };
        if (cmd.type == DrawCmd::Type::Rect) {
            DrawRectangleV({ cmd.x, cmd.y }, { cmd.x2, cmd.y2 }, c);
        } else if (cmd.type == DrawCmd::Type::Text) {
            const std::string fontKey = resolvedFontKey(cmd.fontPath);
            const Font* font = fontKey.empty()
                ? nullptr
                : impl_->resources.font_cache().get(fontKey);
            draw_text_command(cmd, font);
        } else if (cmd.type == DrawCmd::Type::Image) {
            (void)draw_image_command(impl_->resources.texture_cache(),
                                     resolvedTextureKey(cmd.assetId),
                                     cmd);
        }
    }
    EndMode2D();
    impl_->screenQueue.clear();
}

void Renderer::presentScreen() {
    drawScreenPostEffects();
    impl_->surface.end_drawing();
}

void Renderer::endFrame() {
    endWorldPass();
    endScreenPass();
    presentScreen();
}

void Renderer::setScreenShader(const std::string& name) {
    impl_->screenShader = name;
}

void Renderer::drawFadeOverlay(float alpha) {
    if (alpha <= 0.f) return;
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Rect;
    cmd.x = 0.f;
    cmd.y = 0.f;
    cmd.x2 = impl_->scene_logical_viewport().x;
    cmd.y2 = impl_->scene_logical_viewport().y;
    cmd.cr = 0;
    cmd.cg = 0;
    cmd.cb = 0;
    cmd.ca = static_cast<unsigned char>(std::min(255.f, alpha * 255.f));
    impl_->screenQueue.push_back(std::move(cmd));
}

void Renderer::drawScreenPostEffects() {
    if (impl_->screenShader.empty() || impl_->screenShader == "none") return;
    const int w = static_cast<int>(impl_->surface.width());
    const int h = static_cast<int>(impl_->surface.height());
    if (impl_->screenShader == "crt" || impl_->screenShader == "scanlines") {
        for (int y = 0; y < h; y += 4)
            DrawRectangle(0, y, w, 2, Color{ 0, 0, 0, 40 });
    }
}

const ArtCade::Presentation::PresentationSnapshot&
Renderer::committedPresentationSnapshot() const {
    return impl_->lastCommittedPresentation_;
}

uint64_t Renderer::presentationRevision() const {
    return impl_->lastCommittedPresentation_.revision;
}

} // namespace ArtCade::Modules
