#pragma once

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::ProjectJson {

CollisionShape read_collision_shape(const nlohmann::json& json);

bool read_collision_body_component(const nlohmann::json& entityJson,
                                   CollisionBodyComponent& out);

void read_physics_layers(const nlohmann::json& doc,
                         std::vector<PhysicsLayerDef>& out);

void read_collision_profiles(
    const nlohmann::json& doc,
    std::unordered_map<std::string, CollisionProfileDef>& out);

} // namespace ArtCade::ProjectJson
