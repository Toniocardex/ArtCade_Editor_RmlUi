#include "json-primitives.h"

namespace ArtCade::ProjectJson {

Vec2 read_vec2(const nlohmann::json& j, const Vec2& fallback) {
    if (j.is_array() && j.size() >= 2) {
        return {j[0].get<float>(), j[1].get<float>()};
    }
    if (j.is_object()) {
        return {j.value("x", fallback.x), j.value("y", fallback.y)};
    }
    return fallback;
}

Vec3 read_vec3(const nlohmann::json& j, const Vec3& fallback) {
    if (j.is_array() && j.size() >= 3) {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    }
    if (j.is_object()) {
        return {
            j.contains("r") ? j["r"].get<float>() : j.value("x", fallback.x),
            j.contains("g") ? j["g"].get<float>() : j.value("y", fallback.y),
            j.contains("b") ? j["b"].get<float>() : j.value("z", fallback.z),
        };
    }
    return fallback;
}

Vec4 read_vec4(const nlohmann::json& j, const Vec4& fallback) {
    if (j.is_array() && j.size() >= 4) {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }
    if (j.is_object()) {
        return {
            j.contains("r") ? j["r"].get<float>() : j.value("x", fallback.r),
            j.contains("g") ? j["g"].get<float>() : j.value("y", fallback.g),
            j.contains("b") ? j["b"].get<float>() : j.value("z", fallback.b),
            j.contains("a") ? j["a"].get<float>() : j.value("w", fallback.a),
        };
    }
    return fallback;
}

Transform read_transform(const nlohmann::json& j) {
    Transform t;
    if (!j.is_object()) return t;
    if (j.contains("position")) t.position = read_vec2(j["position"]);
    if (j.contains("scale"))    t.scale    = read_vec2(j["scale"], {1.f, 1.f});
    t.rotation = j.value("rotation", 0.f);
    return t;
}

} // namespace ArtCade::ProjectJson
