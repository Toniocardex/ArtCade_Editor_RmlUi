#pragma once

#include "presentation_snapshot.h"
#include "presentation_system.h"

namespace ArtCade::Presentation {

/**
 * Stable picking entry points for editor bindings and gameplay input.
 * Always reads a committed (or historical) snapshot — never live renderer state.
 */
class PresentationBindings {
public:
    static WorldPoint surface_to_world(const PresentationSnapshot& snapshot,
                                       SurfacePoint surface);

    /**
     * Maps surface pixels to world using the active committed snapshot.
     * @param system presentation owner (must have a committed revision)
     */
    static WorldPoint surface_to_world(const PresentationSystem& system,
                                       SurfacePoint surface);

    /**
     * Maps surface pixels using a specific revision when still in history.
     * Falls back to committed snapshot when @p revision is not found.
     */
    static WorldPoint surface_to_world(const PresentationSystem& system,
                                       uint64_t revision,
                                       SurfacePoint surface);
};

} // namespace ArtCade::Presentation
