#include "grid_pass.h"

#include "../scene_frame_snapshot.h"

#include "../editor-overlay-renderer.h"

namespace ArtCade::AppRenderPasses {

void execute_grid_pass(const SceneFrameContext& ctx) {
    if (!ctx.frameSnapshot || !ctx.renderer) return;
    const SceneFrameSnapshot& frame = *ctx.frameSnapshot;
    EditorOverlayRenderer::drawGrid(
        *ctx.renderer, frame.worldSize, frame.overlay);
}

} // namespace ArtCade::AppRenderPasses
