#include "../include/render_pipeline.h"

#include "../../presentation/include/presentation_mode.h"

namespace ArtCade::Modules {

namespace {

using ArtCade::Presentation::PresentationMode;

bool presentation_allows_editor_overlays(PresentationMode mode) {
    return mode == PresentationMode::SceneEdit
        || mode == PresentationMode::CameraPreview;
}

bool presentation_uses_game_view_compositor(PresentationMode mode) {
    return mode == PresentationMode::PlayEmbedded
        || mode == PresentationMode::PlayExternal
        || mode == PresentationMode::PlayFullscreen;
}

} // namespace

RenderPipeline RenderPipelineBuilder::buildPipeline(
    const ArtCade::Presentation::PresentationSnapshot& presentation,
    const ViewRenderFeatures& features,
    bool hasActiveScene) {
    RenderPipeline pipeline{};
    if (!hasActiveScene)
        return pipeline;

    pipeline.captureGameView =
        presentation_uses_game_view_compositor(presentation.effectiveMode);
    pipeline.blitGameView = pipeline.captureGameView;

    if (pipeline.captureGameView)
        pipeline.appPassOrder.push_back(RenderPassId::GameView);

    pipeline.appPassOrder.push_back(RenderPassId::SceneBackdrop);

    const bool editorOverlays =
        presentation_allows_editor_overlays(presentation.effectiveMode);
    if (editorOverlays && features.drawGrid)
        pipeline.appPassOrder.push_back(RenderPassId::Grid);

    pipeline.appPassOrder.push_back(RenderPassId::SceneEntities);

    if (editorOverlays && (features.drawGizmos || features.drawSelection))
        pipeline.appPassOrder.push_back(RenderPassId::Gizmo);

    if (features.drawPhysicsDebug)
        pipeline.appPassOrder.push_back(RenderPassId::Debug);

    if (pipeline.blitGameView)
        pipeline.appPassOrder.push_back(RenderPassId::Blit);

    return pipeline;
}

} // namespace ArtCade::Modules
