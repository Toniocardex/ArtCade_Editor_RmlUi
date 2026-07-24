#include "../src/modules/presentation/include/presentation_mode.h"
#include "../src/modules/presentation/include/presentation_snapshot.h"
#include "../src/modules/renderer/include/render_pipeline.h"
#include "../src/modules/renderer/include/view_render_features.h"

#include <cassert>
#include <vector>

using ArtCade::Modules::RenderPassId;
using ArtCade::Modules::RenderPipeline;
using ArtCade::Modules::RenderPipelineBuilder;
using ArtCade::Modules::ViewRenderFeatures;
using ArtCade::Presentation::PresentationMode;
using ArtCade::Presentation::PresentationSnapshot;

static bool contains(const std::vector<RenderPassId>& order, RenderPassId id) {
    for (RenderPassId entry : order) {
        if (entry == id)
            return true;
    }
    return false;
}

int main() {
    PresentationSnapshot snapshot{};
    ViewRenderFeatures allOn{};
    allOn.drawGrid = true;
    allOn.drawGizmos = true;
    allOn.drawSelection = true;
    allOn.drawPhysicsDebug = true;

    snapshot.effectiveMode = PresentationMode::SceneEdit;
    const RenderPipeline editPipeline =
        RenderPipelineBuilder::buildPipeline(snapshot, allOn, true);
    assert(!editPipeline.captureGameView);
    assert(!editPipeline.blitGameView);
    assert(contains(editPipeline.appPassOrder, RenderPassId::SceneBackdrop));
    assert(contains(editPipeline.appPassOrder, RenderPassId::Grid));
    assert(contains(editPipeline.appPassOrder, RenderPassId::SceneEntities));
    assert(contains(editPipeline.appPassOrder, RenderPassId::Gizmo));
    assert(contains(editPipeline.appPassOrder, RenderPassId::Debug));

    ViewRenderFeatures minimal{};
    const auto sparse = RenderPipelineBuilder::build_pass_order(snapshot, minimal, true);
    assert(contains(sparse, RenderPassId::SceneBackdrop));
    assert(contains(sparse, RenderPassId::SceneEntities));
    assert(!contains(sparse, RenderPassId::Grid));
    assert(!contains(sparse, RenderPassId::Debug));

    snapshot.effectiveMode = PresentationMode::PlayEmbedded;
    const RenderPipeline playPipeline =
        RenderPipelineBuilder::buildPipeline(snapshot, allOn, true);
    assert(playPipeline.captureGameView);
    assert(playPipeline.blitGameView);
    assert(contains(playPipeline.appPassOrder, RenderPassId::GameView));
    assert(contains(playPipeline.appPassOrder, RenderPassId::Blit));
    assert(!contains(playPipeline.appPassOrder, RenderPassId::Grid));
    assert(!contains(playPipeline.appPassOrder, RenderPassId::Gizmo));

    const auto empty = RenderPipelineBuilder::build_pass_order(snapshot, allOn, false);
    assert(empty.empty());
    return 0;
}
