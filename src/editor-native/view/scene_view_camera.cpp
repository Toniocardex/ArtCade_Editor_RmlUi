#include "editor-native/view/scene_view_camera.h"

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

Vec2 screenToWorld(const SceneViewCamera& camera, Vec2 screen) {
    const float zoom = camera.zoom != 0.f ? camera.zoom : 1.f;
    return Vec2{
        (screen.x - camera.offset.x) / zoom + camera.target.x,
        (screen.y - camera.offset.y) / zoom + camera.target.y,
    };
}

} // namespace ArtCade::EditorNative
