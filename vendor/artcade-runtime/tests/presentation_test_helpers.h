#pragma once

#include "../src/modules/renderer/include/renderer.h"
#include "../src/modules/presentation/include/editor_viewport_service.h"

inline void commit_presentation_frame(
    ArtCade::Modules::Renderer& renderer,
    ArtCade::Presentation::EditorViewportService& viewport,
    const ArtCade::SceneDef* scene = nullptr) {
    viewport.sync_from_scene(
        scene,
        renderer.gatherSimulationPresentationInputs(),
        renderer.windowWidth(),
        renderer.windowHeight());
    viewport.begin_frame();
    renderer.applyFramePresentation(viewport.committed_snapshot());
}

/** Commits presentation plus per-frame scene geometry for clip / camera tests. */
inline void commit_test_geometry(
    ArtCade::Modules::Renderer& renderer,
    ArtCade::Presentation::EditorViewportService& viewport,
    const ArtCade::Vec2& worldSize,
    const ArtCade::Vec2& logicalViewport,
    const ArtCade::SceneDef* scene = nullptr) {
    renderer.commitFrameGeometry(worldSize, logicalViewport);
    commit_presentation_frame(renderer, viewport, scene);
}
