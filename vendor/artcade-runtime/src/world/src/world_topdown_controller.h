#pragma once

#include "../../core/types.h"

namespace ArtCade {
class World;
struct TopDownControllerComponent;
}

namespace ArtCade::WorldInternal {

void stepTopDownController(World& world,
                           EntityId id,
                           const TopDownControllerComponent& tc,
                           float dt);

} // namespace ArtCade::WorldInternal
