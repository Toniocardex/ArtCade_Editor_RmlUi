#pragma once

namespace ArtCade::Modules {

class Renderer;

namespace RenderPasses {

/** Blits the GameView RT to the backbuffer using the committed placement. */
void blit_game_view(Renderer& renderer);

} // namespace RenderPasses
} // namespace ArtCade::Modules
