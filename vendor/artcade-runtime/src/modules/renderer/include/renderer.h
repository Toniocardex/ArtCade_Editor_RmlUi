#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include "compositor-layout.h"
#include "../../presentation/include/presentation_snapshot.h"
#include "../../presentation/include/presentation_mode.h"
#include "../../presentation/include/presentation_types.h"
#include "../../presentation/include/presentation_input_builder.h"
#include "view_render_features.h"
#include <functional>
#include <string>
#include <memory>
#include <cstddef>

namespace ArtCade::Modules {

/** Axis-aligned world bounds in framebuffer pixel space (top-left origin). */
struct ScreenClipRect {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
};

/**
 * Renderer — Raylib window + 2D draw API.
 *
 * All Raylib types (Texture2D, Camera2D, Color …) are confined to
 * renderer.cpp via Pimpl — no raylib.h leaks into includers.
 */
class Renderer final : public IModule {
public:
    Renderer();
    ~Renderer();

    bool init()     override;
    void shutdown() override;

    // Must be called before init() to configure window parameters
    void setWindowSize(uint32_t width, uint32_t height, const std::string& title);
    /**
     * Size the native OS window from a logical game viewport using the largest
     * integer scale that fits comfortably on the current monitor.
     */
    void setWindowSizeForLogicalViewport(uint32_t logicalWidth,
                                         uint32_t logicalHeight,
                                         const std::string& title);
    /**
     * Commits per-frame scene geometry (world bounds and logical viewport).
     * Called by beginFrame(); tests may call directly for clip math without drawing.
     */
    void commitFrameGeometry(const Vec2& worldSize, const Vec2& logicalViewport);

    /**
     * Projects world bounds through the active camera (incl. shake).
     * Uses geometry from the last commitFrameGeometry() / beginFrame() call.
     */
    ScreenClipRect worldScreenClipRect() const;

    /** Play mode: render gameplay into a viewport-sized RT, then blit to the window. */
    void setGameViewCompositorEnabled(bool enabled);

    /** Play output scaling policy (fit / fill / stretch). */
    void setOutputPolicy(OutputPolicy policy);
    OutputPolicy outputPolicy() const;
    CompositorLayout compositorLayout() const;

    /** Read-only view of the last snapshot passed to beginFrame(). */
    const ArtCade::Presentation::PresentationSnapshot& committedPresentationSnapshot() const;
    uint64_t presentationRevision() const;

    /**
     * Collects simulation-owned inputs for Presentation Core (game camera,
     * output policy, compositor). Scene geometry comes from SceneDef via
     * presentation_build_inputs() / FrameCoordinator.
     */
    ArtCade::Presentation::PresentationSimulationInputs
    gatherSimulationPresentationInputs() const;

    /**
     * Applies a committed snapshot to Raylib camera state (read-only consumer).
     * @param presentation immutable frame snapshot from EditorViewportService
     */
    void applyFramePresentation(
        const ArtCade::Presentation::PresentationSnapshot& presentation);

    // Frame lifecycle
    void beginFrame(const ArtCade::Presentation::PresentationSnapshot& presentation,
                    const Vec2& worldSize,
                    const Vec2& logicalViewport,
                    const Vec4& clearColor);
    /** Begin the GameView RT pass when the pipeline schedules RenderPassId::GameView. */
    void beginGameViewPass(const Vec4& clearColor);
    void endFrame();
    /** Flush world draw queue and leave 2D camera mode (screen-space draws follow). */
    void endWorldPass();
    /** Flush screen-space (HUD) text queued with drawText(screenSpace=true). */
    void endScreenPass();
    /** Post effects and swap (after screen-space UI / HUD draws). */
    void presentScreen();
    bool shouldClose() const;

    // Discard any draw commands queued by Lua this tick.
    // Called once per fixed-timestep iteration (before luaHost->tick) so that
    // only the LAST tick's drawScene() commands survive to endFrame().
    // Without this, a frame that runs 2+ ticks accumulates draw lists from
    // every tick, which creates a one-frame ghost of objects destroyed
    // mid-frame (e.g. the coin flash on pickup).
    void clearDrawQueue();

    // Draw calls (valid between beginFrame/endFrame)
    void drawSprite(const AssetId& assetId,
                    const Vec2&    position,
                    float          rotation,   // degrees
                    const Vec2&    scale,
                    const Vec4&    tint,
                    const Vec3&    fillColor,
                    float          alpha,
                    const std::string& shaderEffect = "",
                    const Vec2&    pivot = {0.5f, 0.5f},
                    bool           flipX = false,
                    bool           flipY = false);

    /** Width/height `drawSprite` uses (texture pixels × scale, or 32×32 placeholder). */
    Vec2 spriteDestinationSize(const AssetId& assetId, const Vec2& scale) const;

    /** Draw a pixel sub-rectangle of a sprite sheet (animation frame). */
    void drawSpriteFrame(const AssetId& assetId,
                         float srcX, float srcY, float srcW, float srcH,
                         const Vec2&    position,
                         float          rotation,
                         const Vec2&    scale,
                         const Vec4&    tint,
                         float          alpha,
                         const Vec2&    pivot = {0.5f, 0.5f},
                         bool           flipX = false,
                         bool           flipY = false);

    /**
     * Phase F3: draw a sub-rectangle (atlas cell) of a texture at a
     * top-left destination. Returns false if the texture is missing
     * (caller can fall back to a colour rect). No rotation/origin —
     * tiles are axis-aligned and positioned by their top-left corner.
     */
    bool drawSpriteRegion(const AssetId& assetId,
                          float srcX, float srcY, float srcW, float srcH,
                          float dstX, float dstY, float dstW, float dstH,
                          float alpha = 1.f);

    /**
     * Spritesheet Studio: rasterize one atlas sub-rect into RGBA8 (top-left origin).
     * @return 0 on success, negative on failure.
     */
    int captureSpriteRegionFrame(const AssetId& assetId,
                                 float srcX, float srcY, float srcW, float srcH,
                                 int canvasW, int canvasH,
                                 unsigned char* rgbaOut,
                                 int rgbaOutLen);

    void drawRect  (float x, float y, float w, float h, const Vec4& color,
                    bool screenSpace = false);
    void drawRectImmediate(float x, float y, float w, float h, const Vec4& color);
    void drawImage (const AssetId& assetId, float x, float y, float w, float h,
                    float alpha = 1.f, bool screenSpace = false);
    void drawLine  (float x1, float y1, float x2, float y2, const Vec4& color);
    void drawCircle(float x, float y, float radius, const Vec4& color);
    /** align:  0 = left of (x,y), 1 = centered on x, 2 = right of (x,y).
     *  valign: 0 = (x,y) is the top, 1 = vertically centered, 2 = bottom.
     *  screenSpace: true draws fixed on screen (HUD), after the camera pass. */
    void drawText  (const std::string& text, float x, float y,
                    int fontSize, const Vec4& color,
                    const std::string& fontPath = "", int align = 0,
                    bool screenSpace = false, int valign = 0);

    /**
     * Register/replace a GPU texture decoded from an in-memory image buffer
     * under `assetId` (e.g. a tileset uploaded from the editor that is not
     * in the VFS). `ext` is a raylib file-type hint, e.g. ".png".
     * Returns false if the buffer could not be decoded.
     */
    bool registerImageFromMemory(const std::string& assetId,
                                 const unsigned char* data, int len,
                                 const std::string& ext);

    /** Remove a path-keyed texture (LRU / explicit eviction). */
    void invalidateImageAsset(const std::string& assetPath);

    bool registerFontFromMemory(const std::string& path,
                                const unsigned char* data, int len,
                                const std::string& ext,
                                int baseSize = 32);

    void invalidateFontAsset(const std::string& path);

    /** Drop all path-keyed textures and fonts (editor project hot-reload). */
    void evictCachedAssets();

    // GPU texture management
    uint32_t loadTexture  (const std::string& filePath);
    void     unloadTexture(uint32_t handle);
    bool     isTextureLoaded(const AssetId& assetId) const;

    /**
     * Optional resolver: stable asset id or legacy path → TextureCache key (path).
     * Used by native AssetLoader manifest and WASM editor_load_project.
     */
    void setTextureKeyResolver(std::function<std::string(const std::string&)> resolver);
    void setFontKeyResolver(std::function<std::string(const std::string&)> resolver);

private:
    std::string resolvedTextureKey(const std::string& ref) const;
    std::string resolvedFontKey(const std::string& ref) const;

public:
    // 2D camera
    void setCameraPosition(const Vec2& pos);
    /** Center the visible viewport on a world-space point, subject to world bounds. */
    void setCameraCenter(const Vec2& center);
    /** Frame camera modifiers (shake, recoil); included in draw and presentation picking. */
    void setGameCameraModifiers(const ArtCade::Presentation::CameraModifiers& modifiers);
    void setCameraZoom    (float zoom);
    /**
     * Resizes the OS framebuffer from CSS host size × DPR (play preview).
     * Surface metrics for presentation are stored via EditorViewportService.
     */
    void resizePlayFramebuffer(float cssW, float cssH, float devicePixelRatio);

    void panCameraByScreenDelta(float dx, float dy);
    Vec2 visibleWorldSize () const;
    Vec2 getCameraPosition() const;
    /** World-space point currently shown at the center of the viewport. */
    Vec2 getCameraCenter() const;
    float getCameraZoom()   const;

    uint32_t windowWidth()  const;
    uint32_t windowHeight() const;
    float    deltaTime()    const;   // wraps Raylib GetFrameTime()
    /** Toggle borderless fullscreen on native desktop builds. Returns effective play mode. */
    ArtCade::Presentation::PresentationMode toggleBorderlessFullscreen();

    void setScreenShader(const std::string& name);
    void drawFadeOverlay(float alpha);
    void drawScreenPostEffects();

    /** Phase 7 Blit pass — GameView RT → backbuffer using committed placement. */
    void blitGameViewToBackbuffer();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Modules
