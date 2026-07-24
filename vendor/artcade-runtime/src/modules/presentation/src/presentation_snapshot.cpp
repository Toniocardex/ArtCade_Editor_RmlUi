#include "../include/presentation_snapshot.h"

#include "../include/coordinate_mapper.h"

namespace ArtCade::Presentation {

namespace {

OutputPlacement identity_surface_placement(double surfaceW, double surfaceH) {
    OutputPlacement placement{};
    placement.destW = surfaceW;
    placement.destH = surfaceH;
    placement.srcW = surfaceW;
    placement.srcH = surfaceH;
    placement.scaleX = 1.;
    placement.scaleY = 1.;
    return placement;
}

OutputPlacement effective_placement(const PresentationSnapshot& snapshot) {
    if (snapshot.useIdentityPlacement) {
        return identity_surface_placement(snapshot.surface.framebufferWidth,
                                          snapshot.surface.framebufferHeight);
    }
    return snapshot.placement;
}

} // namespace

WorldPoint PresentationSnapshot::surface_to_world(SurfacePoint surface) const {
    return world_from_surface(surface, effective_placement(*this), pickingCamera);
}

SurfacePoint PresentationSnapshot::world_to_surface(WorldPoint world) const {
    return surface_from_world(world, effective_placement(*this), pickingCamera);
}

} // namespace ArtCade::Presentation
