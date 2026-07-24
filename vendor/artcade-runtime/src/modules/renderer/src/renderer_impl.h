#pragma once

#include "../include/renderer.h"
#include "../include/raylib_surface.h"
#include "../include/render_resources.h"
#include "../include/capture_service.h"
#include "../../presentation/include/presentation_mode.h"
#include "../../presentation/include/presentation_types.h"
#include "../../../core/project-defaults.h"
#include "sprite-outline-shader.h"
#include "texture-cache.h"
#include "font-cache.h"

#include <raylib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ArtCade::Modules {

struct DrawCmd {
    enum class Type { Rect, Line, Circle, Text, Image } type = Type::Rect;
    float x  = 0.f, y  = 0.f;
    float x2 = 0.f, y2 = 0.f;
    float r  = 0.f;
    int   fontSize = 20;
    int   align    = 0;
    int   valign   = 0;
    std::string text;
    std::string fontPath;
    std::string assetId;
    unsigned char cr = 255, cg = 255, cb = 255, ca = 255;
};

void draw_text_command(const DrawCmd& cmd, const Font* font);
bool draw_image_command(TextureCache& texCache,
                        const std::string& textureKey,
                        const DrawCmd& cmd);

inline Color renderer_to_color(const Vec4& v, float extraAlpha = 1.f) {
    return Color{
        static_cast<unsigned char>(v.r * 255.f),
        static_cast<unsigned char>(v.g * 255.f),
        static_cast<unsigned char>(v.b * 255.f),
        static_cast<unsigned char>(v.a * extraAlpha * 255.f),
    };
}

inline Vec2 renderer_world_to_screen(const Camera2D& cam, float wx, float wy) {
    const float zoom = (cam.zoom > 0.f) ? cam.zoom : 1.f;
    return {
        (wx - cam.target.x) * zoom + cam.offset.x,
        (wy - cam.target.y) * zoom + cam.offset.y,
    };
}

inline ScreenClipRect renderer_compute_world_clip(const Camera2D& cam,
                                                  const Vec2& worldSize,
                                                  uint32_t fbW,
                                                  uint32_t fbH) {
    if (worldSize.x <= 0.f || worldSize.y <= 0.f || fbW == 0 || fbH == 0)
        return {};

    const Vec2 corners[4] = {
        renderer_world_to_screen(cam, 0.f, 0.f),
        renderer_world_to_screen(cam, worldSize.x, 0.f),
        renderer_world_to_screen(cam, 0.f, worldSize.y),
        renderer_world_to_screen(cam, worldSize.x, worldSize.y),
    };
    float minX = corners[0].x;
    float minY = corners[0].y;
    float maxX = corners[0].x;
    float maxY = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxX = std::max(maxX, corners[i].x);
        maxY = std::max(maxY, corners[i].y);
    }

    const float fbWf = static_cast<float>(fbW);
    const float fbHf = static_cast<float>(fbH);
    const float clipX = std::max(0.f, std::floor(minX));
    const float clipY = std::max(0.f, std::floor(minY));
    const float clipRight = std::min(fbWf, std::ceil(maxX));
    const float clipBottom = std::min(fbHf, std::ceil(maxY));
    const float clipW = clipRight - clipX;
    const float clipH = clipBottom - clipY;
    if (clipW <= 0.f || clipH <= 0.f) return {};
    return { clipX, clipY, clipW, clipH };
}

inline Vec2 renderer_clamp_camera_target(const Vec2& viewportSize,
                                         const Vec2& worldSize,
                                         float cameraZoom,
                                         Vec2 target) {
    const float z = (cameraZoom > 0.f) ? cameraZoom : 1.f;
    const float visibleW = viewportSize.x / z;
    const float visibleH = viewportSize.y / z;
    const float maxX = std::max(0.f, worldSize.x - visibleW);
    const float maxY = std::max(0.f, worldSize.y - visibleH);
    return {
        std::min(std::max(0.f, target.x), maxX),
        std::min(std::max(0.f, target.y), maxY),
    };
}

struct Renderer::Impl {
    RaylibSurface surface;
    RenderResources resources;
    CaptureService capture;

    Vec2 committedWorldSize_{};
    Vec2 committedLogicalViewport_{
        ProjectDefaults::kSceneViewportWidth,
        ProjectDefaults::kSceneViewportHeight,
    };
    bool committedGeometryActive_ = false;

    Camera2D camera = {};
    Camera2D gameViewCamera = {};
    float cameraZoom = 1.f;
    float displayScale = 1.f;
    Vec2 viewportOffset = { 0.f, 0.f };
    Vec2 viewportDrawSize = {
        ProjectDefaults::kSceneViewportWidth,
        ProjectDefaults::kSceneViewportHeight,
    };
    ArtCade::Presentation::GameCameraState storedGameCamera_{};
    ArtCade::Presentation::CameraModifiers gameModifiers_{};
    bool worldScissorActive = false;
    bool worldModeActive = false;
    bool gameViewCompositorEnabled = false;
    bool inGameViewTexturePass = false;
    OutputPolicy outputPolicy = OutputPolicy::Fit;
    CompositorLayout compositorLayout{};
    ArtCade::Presentation::PresentationSnapshot lastCommittedPresentation_{};
    bool hasCommittedPresentation_ = false;

    std::vector<DrawCmd> drawQueue;
    std::vector<DrawCmd> screenQueue;
    std::string screenShader;
    SpriteOutlineShader spriteOutline;

    void apply_projection_from_snapshot(
        const ArtCade::Presentation::PresentationSnapshot& snapshot);
    void apply_frame_presentation(
        const ArtCade::Presentation::PresentationSnapshot& snapshot);
    void begin_world_scissor(const Camera2D& frameCamera);
    void end_world_scissor();
    Camera2D frame_camera_with_shake() const;
    Vec2 scene_world_bounds() const;
    Vec2 scene_logical_viewport() const;

    uint32_t framebuffer_width() const {
        if (inGameViewTexturePass) return resources.game_view_width();
        // ArtCade owns the WASM framebuffer via editor_resize_surface /
        // surface.width_. Prefer that over Raylib GetScreenWidth(), which can
        // drift from the OFFSCREEN_FRAMEBUFFER backing store after re-parent.
        return surface.width();
    }

    uint32_t framebuffer_height() const {
        if (inGameViewTexturePass) return resources.game_view_height();
        return surface.height();
    }
};

} // namespace ArtCade::Modules
