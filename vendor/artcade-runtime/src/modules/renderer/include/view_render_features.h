#pragma once

namespace ArtCade::Modules {

/**
 * Optional overlay passes for a single frame (ADR Phase 7).
 * Scene + GameView capture are scheduled by RenderPipelineBuilder when active.
 */
struct ViewRenderFeatures {
    bool drawGrid         = false;
    bool drawGizmos       = false;
    bool drawSelection    = false;
    bool drawPhysicsDebug = false;
};

} // namespace ArtCade::Modules
