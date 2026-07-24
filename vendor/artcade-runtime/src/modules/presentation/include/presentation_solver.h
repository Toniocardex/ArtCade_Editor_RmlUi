#pragma once

#include "presentation_snapshot.h"
#include "presentation_state.h"

namespace ArtCade::Presentation {

/**
 * Derives placement, picking camera, and overlay metrics from raw presentation
 * inputs. PresentationSystem::calculate_snapshot is the sole consumer.
 */
PresentationSnapshot solve_presentation_snapshot(const PresentationState& state,
                                                 uint64_t revision);

} // namespace ArtCade::Presentation
