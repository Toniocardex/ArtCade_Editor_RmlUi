#pragma once

#include "render_pass_id.h"
#include "view_render_features.h"
#include "../../presentation/include/presentation_snapshot.h"

#include <vector>

namespace ArtCade::Modules {

/** Full per-frame render plan: app-layer pass order plus compositor lifecycle passes. */
struct RenderPipeline {
    std::vector<RenderPassId> appPassOrder;
    bool captureGameView = false;
    bool blitGameView    = false;
};

/**
 * Builds the ordered pass list for a frame from presentation + features.
 */
class RenderPipelineBuilder {
public:
    static RenderPipeline buildPipeline(
        const ArtCade::Presentation::PresentationSnapshot& presentation,
        const ViewRenderFeatures& features,
        bool hasActiveScene);

    /** Returns {@link RenderPipeline::appPassOrder} only. */
    static std::vector<RenderPassId> build_pass_order(
        const ArtCade::Presentation::PresentationSnapshot& presentation,
        const ViewRenderFeatures& features,
        bool hasActiveScene) {
        return buildPipeline(presentation, features, hasActiveScene).appPassOrder;
    }
};

} // namespace ArtCade::Modules
