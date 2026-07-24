#pragma once
// =============================================================================
// sprite-json — deserialize SpriteComponent from editor project JSON
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>

namespace ArtCade::ProjectJson {

/**
 * Reads `entityJson["sprite"]` when present.
 * @param entityJson  entity or objectType object from project.json
 * @param out         filled when the function returns true
 * @return true when a sprite object was parsed into `out`
 */
bool read_sprite_component(const nlohmann::json& entityJson,
                           SpriteComponent& out);

} // namespace ArtCade::ProjectJson
