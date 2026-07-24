#pragma once
// =============================================================================
// json-primitives — shared nlohmann helpers for project.json parsing
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ArtCade::ProjectJson {

Vec2 read_vec2(const nlohmann::json& j, const Vec2& fallback = {0.f, 0.f});
Vec3 read_vec3(const nlohmann::json& j, const Vec3& fallback = {1.f, 1.f, 1.f});
Vec4 read_vec4(const nlohmann::json& j, const Vec4& fallback = {1.f, 1.f, 1.f, 1.f});

Transform read_transform(const nlohmann::json& j);

} // namespace ArtCade::ProjectJson
