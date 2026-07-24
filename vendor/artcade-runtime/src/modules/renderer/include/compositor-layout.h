#pragma once

#include "../../presentation/include/output_policy.h"
#include "../../presentation/include/presentation_types.h"

namespace ArtCade::Modules {

/** @deprecated Use ArtCade::Presentation::OutputPlacement — removed after Phase 4. */
using CompositorLayout = ArtCade::Presentation::OutputPlacement;

/**
 * @deprecated Use ArtCade::Presentation::output_placement_compute — removed after Phase 4.
 */
[[deprecated("Use ArtCade::Presentation::output_placement_compute")]]
inline CompositorLayout compositor_layout(float backW, float backH,
                                        float viewW, float viewH,
                                        OutputPolicy policy) {
    return ArtCade::Presentation::output_placement_compute(
        static_cast<double>(backW),
        static_cast<double>(backH),
        static_cast<double>(viewW),
        static_cast<double>(viewH),
        policy);
}

} // namespace ArtCade::Modules
