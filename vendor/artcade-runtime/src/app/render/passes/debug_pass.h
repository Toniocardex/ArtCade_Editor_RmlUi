#pragma once

namespace ArtCade {
class World;
namespace Modules { class Renderer; }
}

namespace ArtCade::AppRenderPasses {

/** Physics collision debug overlay (play mode). */
void execute_debug_pass(Modules::Renderer& renderer, const World& world);

} // namespace ArtCade::AppRenderPasses
