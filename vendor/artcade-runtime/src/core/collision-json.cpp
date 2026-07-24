#include "collision-json.h"

#include "json-primitives.h"
#include "project-meta-json.h"

#include <algorithm>

namespace ArtCade::ProjectJson {

namespace {

CollisionShapeType read_shape_type(const std::string& value) {
    if (value == "circle") return CollisionShapeType::Circle;
    if (value == "capsule") return CollisionShapeType::Capsule;
    if (value == "polygon") return CollisionShapeType::Polygon;
    return CollisionShapeType::Rectangle;
}

CollisionResponse read_response(const std::string& value) {
    if (value == "sensor") return CollisionResponse::Sensor;
    return CollisionResponse::Solid;
}

CollisionShapeRole read_role(const std::string& value) {
    if (value == "feet") return CollisionShapeRole::Feet;
    if (value == "hurtbox") return CollisionShapeRole::Hurtbox;
    if (value == "hitbox") return CollisionShapeRole::Hitbox;
    if (value == "interaction") return CollisionShapeRole::Interaction;
    return CollisionShapeRole::Body;
}

BodyType read_body_type(const std::string& value) {
    if (value == "static") return BodyType::Static;
    if (value == "kinematic") return BodyType::Kinematic;
    return BodyType::Dynamic;
}

std::vector<std::string> read_string_array(const nlohmann::json& json,
                                           const std::vector<std::string>& fallback) {
    if (!json.is_array()) return fallback;
    std::vector<std::string> out;
    out.reserve(json.size());
    for (const auto& item : json) {
        if (item.is_string())
            out.push_back(item.get<std::string>());
    }
    return out.empty() ? fallback : out;
}

std::vector<Vec2> read_points(const nlohmann::json& json) {
    std::vector<Vec2> out;
    if (!json.is_array()) return out;
    out.reserve(json.size());
    for (const auto& item : json)
        out.push_back(read_vec2(item));
    return out;
}

std::vector<CollisionShape> read_shapes(const nlohmann::json& json) {
    std::vector<CollisionShape> out;
    if (!json.is_array()) return out;
    out.reserve(json.size());
    for (const auto& item : json) {
        if (item.is_object())
            out.push_back(read_collision_shape(item));
    }
    return out;
}

} // namespace

CollisionShape read_collision_shape(const nlohmann::json& json) {
    CollisionShape shape;
    if (!json.is_object()) return shape;

    shape.type = read_shape_type(json.value("type", std::string("rectangle")));
    shape.response = read_response(json.value("response", std::string("solid")));
    shape.role = read_role(json.value("role", std::string("body")));
    shape.layerId = json.value("layerId", std::string("default"));
    shape.maskLayerIds = read_string_array(
        json.contains("maskLayerIds") ? json["maskLayerIds"] : nlohmann::json{},
        { "default" });

    if (json.contains("offset")) shape.offset = read_vec2(json["offset"]);
    shape.offset.x = json.value("offsetX", shape.offset.x);
    shape.offset.y = json.value("offsetY", shape.offset.y);

    if (json.contains("size")) shape.size = read_vec2(json["size"], shape.size);
    shape.size.x = json.value("width", shape.size.x);
    shape.size.y = json.value("height", shape.size.y);
    shape.radius = json.value("radius", shape.radius);
    if (json.contains("points")) shape.points = read_points(json["points"]);

    shape.enabled = json.value("enabled", true);
    shape.oneWay = json.value("oneWay", false);
    shape.friction = json.value("friction", shape.friction);
    shape.restitution = json.value("restitution", shape.restitution);
    shape.density = json.value("density", shape.density);
    return shape;
}

bool read_collision_body_component(const nlohmann::json& entityJson,
                                   CollisionBodyComponent& out) {
    if (!entityJson.contains("collisionBody") || !entityJson["collisionBody"].is_object())
        return false;

    const auto& raw = entityJson["collisionBody"];
    CollisionBodyComponent body;
    body.bodyType = read_body_type(raw.value("bodyType", std::string("static")));
    body.enabled = raw.value("enabled", true);
    body.profileId = raw.value("profileId", std::string{});
    body.shapes = read_shapes(raw.contains("shapes") ? raw["shapes"] : nlohmann::json{});
    if (body.shapes.empty() && body.profileId.empty())
        body.shapes.push_back(CollisionShape{});
    out = std::move(body);
    return true;
}

void read_physics_layers(const nlohmann::json& doc,
                         std::vector<PhysicsLayerDef>& out) {
    out.clear();
    const nlohmann::json* raw = nullptr;
    if (doc.contains("physics") && doc["physics"].is_object()
        && doc["physics"].contains("layers") && doc["physics"]["layers"].is_array()) {
        raw = &doc["physics"]["layers"];
    }

    if (!raw) {
        out = {
            { "default", "Default", 0, hex_to_vec4("#8B95A7") },
            { "player", "Player", 1, hex_to_vec4("#38BDF8") },
            { "ground", "Ground", 2, hex_to_vec4("#22C55E") },
            { "enemy", "Enemy", 3, hex_to_vec4("#F97316") },
            { "pickup", "Pickup", 4, hex_to_vec4("#FBBF24") },
            { "hazard", "Hazard", 5, hex_to_vec4("#EF4444") },
            { "projectile", "Projectile", 6, hex_to_vec4("#A78BFA") },
            { "interaction", "Interaction", 7, hex_to_vec4("#14B8A6") },
        };
        return;
    }

    out.reserve(raw->size());
    uint32_t fallbackBit = 0;
    for (const auto& item : *raw) {
        if (!item.is_object()) continue;
        PhysicsLayerDef layer;
        layer.id = item.value("id", std::string{});
        layer.name = item.value("name", layer.id);
        layer.bit = item.value("bit", fallbackBit);
        if (item.contains("color") && item["color"].is_string())
            layer.color = hex_to_vec4(item["color"].get<std::string>());
        else if (item.contains("color"))
            layer.color = read_vec4(item["color"], layer.color);
        if (!layer.id.empty() && layer.bit < 32)
            out.push_back(std::move(layer));
        ++fallbackBit;
    }
}

void read_collision_profiles(
    const nlohmann::json& doc,
    std::unordered_map<std::string, CollisionProfileDef>& out) {
    out.clear();
    if (!doc.contains("collisionProfiles") || !doc["collisionProfiles"].is_object())
        return;

    for (auto& [key, value] : doc["collisionProfiles"].items()) {
        if (!value.is_object()) continue;
        CollisionProfileDef profile;
        profile.id = value.value("id", key);
        profile.name = value.value("name", profile.id);
        const std::string space = value.value("coordinateSpace", std::string("frame-normalized"));
        profile.coordinateSpace =
            (space == "world" || space == "World")
                ? CollisionProfileCoordinateSpace::World
                : CollisionProfileCoordinateSpace::FrameNormalized;
        profile.shapes = read_shapes(value.contains("shapes") ? value["shapes"] : nlohmann::json{});

        if (value.contains("perAnimation") && value["perAnimation"].is_object()) {
            for (auto& [anim, shapesJson] : value["perAnimation"].items())
                profile.perAnimation[anim] = read_shapes(shapesJson);
        }
        if (value.contains("perFrame") && value["perFrame"].is_object()) {
            for (auto& [frame, shapesJson] : value["perFrame"].items())
                profile.perFrame[frame] = read_shapes(shapesJson);
        }
        if (!profile.id.empty())
            out[profile.id] = std::move(profile);
    }
}

} // namespace ArtCade::ProjectJson
