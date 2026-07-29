#include "editor-overlay-renderer.h"

#include "../../modules/renderer/include/renderer.h"

#include <algorithm>

namespace ArtCade::EditorOverlayRenderer {
namespace {

void drawRectOutline(Modules::Renderer& renderer,
                     float x, float y, float w, float h,
                     const Vec4& color) {
    constexpr float thickness = 2.f;
    renderer.drawRectImmediate(x, y, w, thickness, color);
    renderer.drawRectImmediate(x, y + h - thickness, w, thickness, color);
    renderer.drawRectImmediate(x, y, thickness, h, color);
    renderer.drawRectImmediate(x + w - thickness, y, thickness, h, color);
}

} // namespace

void drawBackdrop(Modules::Renderer& renderer,
                  const Vec4& backgroundColor,
                  const EditorOverlayState& state) {
    if (!state.inEditMode) return;

    const Vec2 camera = renderer.getCameraPosition();
    const Vec2 visible = renderer.visibleWorldSize();
    renderer.drawRectImmediate(
        camera.x, camera.y,
        std::max(1.f, visible.x), std::max(1.f, visible.y), backgroundColor);
}

void drawGrid(Modules::Renderer& renderer,
              const Vec2& worldSize,
              const EditorOverlayState& state) {
    if (!state.inEditMode || !state.guidesEnabled) return;

    const float width = std::max(1.f, worldSize.x);
    const float height = std::max(1.f, worldSize.y);
    drawRectOutline(renderer, 0.f, 0.f, width, height, {0.92f, 0.92f, 0.94f, 0.6f});

    const float step = state.gridSize > 0.f ? state.gridSize : 32.f;
    if (step < 4.f) return;

    const Vec4 grid{0.918f, 0.918f, 0.918f, 0.55f};
    for (float x = step; x < width; x += step)
        renderer.drawRectImmediate(x, 0.f, 1.f, height, grid);
    for (float y = step; y < height; y += step)
        renderer.drawRectImmediate(0.f, y, width, 1.f, grid);
}

void drawBackdrop(Modules::Renderer& renderer,
                  const SceneDef& scene,
                  const EditorOverlayState& state) {
    drawBackdrop(renderer, scene.backgroundColor, state);
}

void drawGrid(Modules::Renderer& renderer,
              const SceneDef& scene,
              const EditorOverlayState& state) {
    drawGrid(renderer, scene.worldSize, state);
}

} // namespace ArtCade::EditorOverlayRenderer
