#pragma once

#include "presentation_types.h"

#include <cstdint>

namespace ArtCade::Presentation {

/**
 * Pointer sample in surface space with the presentation revision captured by the browser.
 * Used for picking and coordinate mapping during rapid resize / mode transitions.
 */
struct SurfacePointerEvent {
    SurfacePoint position{};
    uint64_t presentationRevision = 0;
};

} // namespace ArtCade::Presentation
