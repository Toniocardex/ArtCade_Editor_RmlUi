#include "../../include/passes/blit_pass.h"
#include "../../include/renderer.h"

namespace ArtCade::Modules::RenderPasses {

void blit_game_view(Renderer& renderer) {
    renderer.blitGameViewToBackbuffer();
}

} // namespace ArtCade::Modules::RenderPasses
