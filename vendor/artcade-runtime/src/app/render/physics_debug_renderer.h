#pragma once

namespace ArtCade::Modules {
class Renderer;
}

namespace ArtCade {
class World;
}

namespace ArtCade::AppRender {

/** Draw CollisionWorld shapes/events generated from entities and tilemaps. */
void drawCollisionDebug(Modules::Renderer& renderer,
                        const World& world);

} // namespace ArtCade::AppRender
