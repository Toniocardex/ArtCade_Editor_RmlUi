#pragma once

#include "../../core/types.h"

namespace ArtCade {
class World;
struct PlatformerControllerComponent;
}

namespace ArtCade::WorldInternal {

void stepPlatformerController(World& world,
                              EntityId id,
                              const PlatformerControllerComponent& pc,
                              float dt);

} // namespace ArtCade::WorldInternal
