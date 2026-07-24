#include "debug_pass.h"

#include "../physics_debug_renderer.h"

namespace ArtCade::AppRenderPasses {

void execute_debug_pass(Modules::Renderer& renderer, const World& world) {
    AppRender::drawCollisionDebug(renderer, world);
}

} // namespace ArtCade::AppRenderPasses
