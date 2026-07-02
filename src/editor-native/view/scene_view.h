#pragma once

#include "editor-native/model/editor_state.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

class TextureCache;

// SceneView draws an immutable scene frame projection into a viewport rect.
// It never reads ProjectDocument or editor panels during draw; GPU resources are
// queried through TextureCache, a derived rendering cache.
class SceneView {
public:
    void render(const SceneFrameSnapshot& frame,
                const EditorSceneViewState& view,
                const ViewportRect& rect,
                const TextureCache& textures) const;
};

} // namespace ArtCade::EditorNative
