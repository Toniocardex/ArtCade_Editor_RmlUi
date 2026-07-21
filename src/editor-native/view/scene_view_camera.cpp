#include "editor-native/view/scene_view_camera.h"

#include <algorithm>

namespace ArtCade::EditorNative {

SceneViewCamera makeSceneViewCamera(const ViewportRect& rect,
                                    const EditorSceneViewState& view,
                                    Vec2 worldSize) {
    SceneViewCamera camera;
    camera.offset = Vec2{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    camera.target = Vec2{worldSize.x * 0.5f + view.pan.x, worldSize.y * 0.5f + view.pan.y};
    camera.zoom   = view.zoom;
    return camera;
}

float computeFitZoom(Vec2 worldSize, const ViewportRect& rect, float padding) {
    if (worldSize.x <= 0.f || worldSize.y <= 0.f) return 1.f;
    const float availW = std::max(1.f, static_cast<float>(rect.width) - padding * 2.f);
    const float availH = std::max(1.f, static_cast<float>(rect.height) - padding * 2.f);
    return std::min(availW / worldSize.x, availH / worldSize.y);
}

Vec2 screenToWorld(const SceneViewCamera& camera, Vec2 screen) {
    const float zoom = camera.zoom != 0.f ? camera.zoom : 1.f;
    return Vec2{
        (screen.x - camera.offset.x) / zoom + camera.target.x,
        (screen.y - camera.offset.y) / zoom + camera.target.y,
    };
}

SceneViewportProjection resolveSceneViewportProjection(const ViewportRect& visibleRect,
                                                        const ViewportRect& cameraAnchorRect,
                                                        const EditorSceneViewState& view,
                                                        Vec2 worldSize) {
    SceneViewportProjection projection;
    projection.visibleRect = visibleRect;
    projection.cameraAnchorRect = cameraAnchorRect;
    projection.camera = makeSceneViewCamera(cameraAnchorRect, view, worldSize);
    return projection;
}

} // namespace ArtCade::EditorNative
