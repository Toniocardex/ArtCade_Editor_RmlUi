#pragma once

#include "editor-overlay-renderer.h"
#include "scene_frame_snapshot.h"

namespace ArtCade {

namespace Modules {
class Renderer;
}
namespace Presentation {
class EditorViewportService;
}

/**
 * Frame-boundary coordinator: presentation commit + SceneFrameSnapshot build.
 * Sole path for presentation input composition from scene authority (PR7).
 */
struct FrameCoordinatorInput {
    uint64_t frameNumber = 0;
    uint64_t sceneRevision = 0;
    const SceneDef* activeScene = nullptr;
    Modules::Renderer* renderer = nullptr;
    Presentation::EditorViewportService* editorViewport = nullptr;
    EditorOverlayState overlay{};
    float sceneFadeAlpha = 0.f;
};

/**
 * Commits presentation for the frame and returns the immutable scene frame snapshot.
 * @param input active scene and wired services (renderer/viewport must be non-null)
 */
SceneFrameSnapshot frame_coordinator_build_frame(const FrameCoordinatorInput& input);

} // namespace ArtCade
