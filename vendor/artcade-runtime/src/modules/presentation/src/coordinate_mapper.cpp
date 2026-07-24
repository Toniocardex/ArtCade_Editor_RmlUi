#include "../include/coordinate_mapper.h"

#include <algorithm>

namespace ArtCade::Presentation {

namespace {

double safe_scale(double scale) {
    return (scale > 0.) ? scale : 1.;
}

double safe_zoom(double zoom) {
    return (zoom > 0.) ? zoom : 1.;
}

} // namespace

LogicalPoint logical_from_surface(SurfacePoint surface,
                                  const OutputPlacement& placement) {
    const double scaleX = safe_scale(placement.scaleX);
    const double scaleY = safe_scale(placement.scaleY);
    return {
        placement.srcX + (surface.x - placement.destX) / scaleX,
        placement.srcY + (surface.y - placement.destY) / scaleY,
    };
}

SurfacePoint surface_from_logical(LogicalPoint logical,
                                  const OutputPlacement& placement) {
    const double scaleX = safe_scale(placement.scaleX);
    const double scaleY = safe_scale(placement.scaleY);
    return {
        placement.destX + (logical.x - placement.srcX) * scaleX,
        placement.destY + (logical.y - placement.srcY) * scaleY,
    };
}

WorldPoint world_from_logical(LogicalPoint logical, const ViewCamera2D& camera) {
    const double zoom = safe_zoom(camera.zoom);
    return {
        (logical.x - camera.offsetX) / zoom + camera.targetX,
        (logical.y - camera.offsetY) / zoom + camera.targetY,
    };
}

LogicalPoint logical_from_world(WorldPoint world, const ViewCamera2D& camera) {
    const double zoom = safe_zoom(camera.zoom);
    return {
        (world.x - camera.targetX) * zoom + camera.offsetX,
        (world.y - camera.targetY) * zoom + camera.offsetY,
    };
}

WorldPoint world_from_surface(SurfacePoint surface,
                              const OutputPlacement& placement,
                              const ViewCamera2D& camera) {
    return world_from_logical(logical_from_surface(surface, placement), camera);
}

SurfacePoint surface_from_world(WorldPoint world,
                                const OutputPlacement& placement,
                                const ViewCamera2D& camera) {
    return surface_from_logical(logical_from_world(world, camera), placement);
}

} // namespace ArtCade::Presentation
