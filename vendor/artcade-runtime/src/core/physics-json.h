#pragma once
// =============================================================================
// physics-json — deserialize PhysicsComponent from editor project JSON
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>

namespace ArtCade::ProjectJson {

/**
 * Reads `entityJson["physics"]` when present.
 * @param entityJson  entity or objectType object from project.json
 * @param out         filled when the function returns true
 * @return true when a physics object was parsed into `out`
 */
bool read_physics_component(const nlohmann::json& entityJson,
                            PhysicsComponent& out);

} // namespace ArtCade::ProjectJson
