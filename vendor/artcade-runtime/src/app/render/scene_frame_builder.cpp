#include "scene_frame_snapshot.h"

namespace ArtCade {

SceneFrameSnapshot scene_frame_build(const SceneFrameBuildInput& input) {
    SceneFrameSnapshot snap{};
    snap.frameNumber = input.frameNumber;
    snap.sceneRevision = input.sceneRevision;
    snap.presentation = input.presentation;
    snap.presentationRevision = input.presentation.revision;
    snap.overlay = input.overlay;
    snap.sceneFadeAlpha = input.sceneFadeAlpha;

    if (!input.activeScene) return snap;

    snap.sceneId = input.activeScene->id;
    snap.worldSize = input.activeScene->worldSize;
    snap.logicalViewport = input.activeScene->viewportSize;
    snap.backgroundColor = input.activeScene->backgroundColor;
    snap.layerSettings = input.activeScene->layerSettings;
    snap.tilemap = &input.activeScene->tilemap;
    snap.tilemapLayers = &input.activeScene->tilemapLayers;
    return snap;
}

} // namespace ArtCade
