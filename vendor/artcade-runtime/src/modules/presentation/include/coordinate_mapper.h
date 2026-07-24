#pragma once

#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Maps a framebuffer point into logical viewport space using output placement.
 * Points in letterbox margins map outside [0, srcW) × [0, srcH).
 */
LogicalPoint logical_from_surface(SurfacePoint surface,
                                  const OutputPlacement& placement);

/** Inverse of logical_from_surface for the active placement scale. */
SurfacePoint surface_from_logical(LogicalPoint logical,
                                  const OutputPlacement& placement);

/**
 * Maps logical viewport pixels to world using a 2D camera (Raylib semantics).
 * world = (logical - offset) / zoom + target
 */
WorldPoint world_from_logical(LogicalPoint logical, const ViewCamera2D& camera);

/** Inverse of world_from_logical. */
LogicalPoint logical_from_world(WorldPoint world, const ViewCamera2D& camera);

/** Surface → logical → world in one step. */
WorldPoint world_from_surface(SurfacePoint surface,
                              const OutputPlacement& placement,
                              const ViewCamera2D& camera);

/** World → logical → surface in one step. */
SurfacePoint surface_from_world(WorldPoint world,
                                const OutputPlacement& placement,
                                const ViewCamera2D& camera);

} // namespace ArtCade::Presentation
