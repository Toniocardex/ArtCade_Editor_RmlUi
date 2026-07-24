#include "../include/presentation_bindings.h"

namespace ArtCade::Presentation {

WorldPoint PresentationBindings::surface_to_world(
    const PresentationSnapshot& snapshot,
    SurfacePoint surface) {
    return snapshot.surface_to_world(surface);
}

WorldPoint PresentationBindings::surface_to_world(
    const PresentationSystem& system,
    SurfacePoint surface) {
    return system.committed_snapshot().surface_to_world(surface);
}

WorldPoint PresentationBindings::surface_to_world(
    const PresentationSystem& system,
    uint64_t revision,
    SurfacePoint surface) {
    if (const PresentationSnapshot* historical = system.find_snapshot(revision))
        return historical->surface_to_world(surface);
    return system.committed_snapshot().surface_to_world(surface);
}

} // namespace ArtCade::Presentation
