#include "project-meta-json.h"

#include "collision-json.h"
#include "json-primitives.h"
#include "entity-json.h"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace ArtCade::ProjectJson {

namespace {

constexpr Vec4 kFallbackHexColor{0.5f, 0.5f, 0.5f, 1.f};

std::optional<int> parse_hex_byte(std::string_view pair) {
    if (pair.size() != 2)
        return std::nullopt;
    for (char c : pair) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
    }
    return std::stoi(std::string(pair), nullptr, 16);
}

PhysicsMode read_physics_mode(const nlohmann::json& worldJson) {
    const std::string mode = worldJson.value("physicsMode", "auto");
    if (mode == "off") return PhysicsMode::Off;
    if (mode == "on")  return PhysicsMode::On;
    return PhysicsMode::Auto;
}

OutputPolicy read_output_policy(const nlohmann::json& worldJson) {
    const std::string policy = worldJson.value("outputPolicy", "fit");
    if (policy == "fill") return OutputPolicy::Fill;
    if (policy == "stretch") return OutputPolicy::Stretch;
    return OutputPolicy::Fit;
}

Vec4 read_tile_palette_color(const nlohmann::json& item) {
    if (!item.contains("color"))
        return hex_to_vec4("#808080");
    const auto& color = item["color"];
    if (color.is_string())
        return hex_to_vec4(color.get<std::string>());
    return read_vec4(color);
}

bool read_tile_palette_entry(const nlohmann::json& item, TilePaletteEntry& out) {
    if (!item.is_object())
        return false;

    out = TilePaletteEntry{};
    out.id = item.value("id", 0);
    if (out.id < 1)
        return false;

    out.name        = item.value("name", std::string{});
    out.color       = read_tile_palette_color(item);
    CollisionBodyComponent body{};
    if (read_collision_body_component(item, body))
        out.collisionBody = std::move(body);
    return true;
}

} // namespace

Vec4 hex_to_vec4(const std::string& hex) {
    auto h = hex;
    if (!h.empty() && h[0] == '#')
        h.erase(0, 1);
    if (h.size() == 3)
        h = std::string{h[0], h[0], h[1], h[1], h[2], h[2]};
    if (h.size() != 6)
        return kFallbackHexColor;

    const std::optional<int> r = parse_hex_byte(h.substr(0, 2));
    const std::optional<int> g = parse_hex_byte(h.substr(2, 2));
    const std::optional<int> b = parse_hex_byte(h.substr(4, 2));
    if (!r.has_value() || !g.has_value() || !b.has_value())
        return kFallbackHexColor;

    return {
        static_cast<float>(*r) / 255.f,
        static_cast<float>(*g) / 255.f,
        static_cast<float>(*b) / 255.f,
        1.f,
    };
}

void read_world_settings(const nlohmann::json& worldJson, WorldSettings& out) {
    if (!worldJson.is_object())
        return;

    out.gravity = worldJson.value("gravity", 9.81f);
    out.pixelsPerMeter = worldJson.value("pixelsPerMeter", 100.f);
    out.timeScale = worldJson.value("timeScale", 1.f);
    out.physicsMode    = read_physics_mode(worldJson);
    out.outputPolicy   = read_output_policy(worldJson);

    if (worldJson.contains("physicsDebugDraw") && worldJson["physicsDebugDraw"].is_boolean())
        out.physicsDebugDraw = worldJson["physicsDebugDraw"].get<bool>();
}

void read_tile_palette(const nlohmann::json& doc, std::vector<TilePaletteEntry>& out) {
    out.clear();

    const nlohmann::json* raw = nullptr;
    if (doc.contains("tilePalette") && doc["tilePalette"].is_array())
        raw = &doc["tilePalette"];
    if (raw == nullptr)
        return;

    out.reserve(raw->size());
    for (const auto& item : *raw) {
        TilePaletteEntry entry;
        if (read_tile_palette_entry(item, entry))
            out.push_back(std::move(entry));
    }
}

void read_tileset_asset(const nlohmann::json& tilesetJson,
                        const std::string& mapKey,
                        TilesetAsset& out) {
    out.assetId = tilesetJson.value("assetId", mapKey);
    if (out.assetId.empty())
        out.assetId = mapKey;
    out.name = tilesetJson.value("name", mapKey);
    out.imageAssetId = tilesetJson.value("imageAssetId", std::string{});
    if (out.imageAssetId.empty()) {
        out.imageAssetId = tilesetJson.value("spriteImagePath", std::string{});
    }
    if (tilesetJson.contains("slicing") && tilesetJson["slicing"].is_object()) {
        const auto& s = tilesetJson["slicing"];
        out.slicing.tileWidth = s.value("tileWidth", out.slicing.tileWidth);
        out.slicing.tileHeight = s.value("tileHeight", out.slicing.tileHeight);
        out.slicing.marginX = s.value("marginX", out.slicing.marginX);
        out.slicing.marginY = s.value("marginY", out.slicing.marginY);
        out.slicing.spacingX = s.value("spacingX", out.slicing.spacingX);
        out.slicing.spacingY = s.value("spacingY", out.slicing.spacingY);
    } else {
        // Legacy flat layout (pre-nested-slicing fixtures/projects).
        const float tileW = tilesetJson.value("tileSize", 32.f);
        const float tileH = tilesetJson.value("tileHeight", tileW);
        out.slicing.tileWidth = static_cast<int>(tileW > 0.f ? tileW : 32.f);
        out.slicing.tileHeight = static_cast<int>(tileH > 0.f ? tileH : tileW);
        const int legacyMargin = tilesetJson.value("margin", 0);
        out.slicing.marginX = legacyMargin;
        out.slicing.marginY = legacyMargin;
    }
    out.tiles.clear();
    if (tilesetJson.contains("tiles") && tilesetJson["tiles"].is_array()) {
        for (const auto& t : tilesetJson["tiles"]) {
            if (!t.is_object()) continue;
            TileDefinition tile;
            tile.id = t.value("id", std::string{});
            tile.x = t.value("x", 0);
            tile.y = t.value("y", 0);
            tile.width = t.value("width", 0);
            tile.height = t.value("height", 0);
            if (!tile.id.empty()) out.tiles.push_back(std::move(tile));
        }
    }
}

void read_tilesets(const nlohmann::json& doc, std::vector<TilesetAsset>& out) {
    out.clear();
    if (!doc.contains("tilesets"))
        return;

    const auto& tsj = doc["tilesets"];
    if (tsj.is_array()) {
        out.reserve(tsj.size());
        for (const auto& item : tsj) {
            TilesetAsset tileset;
            read_tileset_asset(item, {}, tileset);
            out.push_back(std::move(tileset));
        }
    } else if (tsj.is_object()) {
        out.reserve(tsj.size());
        for (auto& [key, val] : tsj.items()) {
            if (!val.is_object())
                continue;
            TilesetAsset tileset;
            read_tileset_asset(val, key, tileset);
            out.push_back(std::move(tileset));
        }
    }
}

void read_thumbnails(const nlohmann::json& doc,
                     std::unordered_map<SceneId, std::string>& out) {
    out.clear();
    if (!doc.contains("thumbnails") || !doc["thumbnails"].is_object())
        return;

    for (auto& [sceneId, thumbPath] : doc["thumbnails"].items()) {
        if (thumbPath.is_string())
            out[sceneId] = thumbPath.get<std::string>();
    }
}

void read_project_header(const nlohmann::json& doc, ProjectDoc& out) {
    out.projectName   = doc.value("projectName", "Untitled");
    out.version       = doc.value("version", "2.0.0");
    out.licenseTier = doc.value("licenseTier", "free");
    out.targetFPS = doc.value("targetFPS", 60.f);
    out.activeSceneId = doc.value("activeSceneId", std::string{});
    out.mainScriptPath = doc.value("mainScriptPath", "scripts/main.lua");
    out.formatVersion = doc.value("formatVersion", 0);
    out.scriptAssets.clear();
    if (doc.contains("scriptAssets") && doc["scriptAssets"].is_array()) {
        for (const auto& item : doc["scriptAssets"]) {
            if (!item.is_object()) continue;
            ScriptAssetDef asset;
            asset.assetId = item.value("assetId", std::string{});
            if (asset.assetId.empty()) asset.assetId = item.value("id", std::string{});
            asset.name = item.value("name", asset.assetId);
            asset.sourcePath = item.value("sourcePath", std::string{});
            out.scriptAssets.push_back(std::move(asset));
        }
    }
}

void read_global_variables(const nlohmann::json& doc, ProjectDoc& out) {
    const nlohmann::json& raw = doc.at("globalVariables");
    out.globalVariables.clear();
    out.globalVariables.reserve(raw.size());
    for (const nlohmann::json& item : raw) {
        GameVariableDefinition definition;
        definition.key = item.at("key").get<std::string>();
        const std::string type = item.at("type").get<std::string>();
        if (type == "number") {
            definition.type = GameVariableDefinition::Type::Number;
            definition.initialValue = item.at("initialValue").get<double>();
        } else if (type == "boolean") {
            definition.type = GameVariableDefinition::Type::Boolean;
            definition.initialValue = item.at("initialValue").get<bool>();
        } else if (type == "string") {
            definition.type = GameVariableDefinition::Type::String;
            definition.initialValue = item.at("initialValue").get<std::string>();
        } else {
            throw std::logic_error("validated global variable has unsupported type");
        }
        if (item.contains("description"))
            definition.description = item.at("description").get<std::string>();
        out.globalVariables.push_back(std::move(definition));
    }
}

void read_scene_layers(const nlohmann::json& container, std::vector<SceneLayerDef>& out) {
    out.clear();
    // Container is a scene object; layers are scoped to their SceneDef.
    if (!container.contains("layers") || !container["layers"].is_array())
        return;

    for (const auto& item : container["layers"]) {
        SceneLayerDef layer;
        if (item.is_object()) {
            layer.id     = item.value("id", std::string{});
            layer.name   = item.value("name", std::string{});
            layer.locked = item.value("locked", false);
        }
        if (!layer.id.empty() && !layer.name.empty())
            out.push_back(std::move(layer));
    }
}

void read_runtime_settings(const nlohmann::json& doc, ProjectRuntimeSettings& out) {
    if (doc.contains("targetFPS"))
        out.targetFPS = doc["targetFPS"].get<float>();

    if (!doc.contains("world") || !doc["world"].is_object())
        return;

    const auto& worldJson = doc["world"];
    if (worldJson.contains("gravity"))
        out.gravity = worldJson["gravity"].get<float>();
    if (worldJson.contains("pixelsPerMeter"))
        out.pixelsPerMeter = worldJson["pixelsPerMeter"].get<float>();
    if (worldJson.contains("timeScale"))
        out.timeScale = worldJson["timeScale"].get<float>();
    if (worldJson.contains("physicsDebugDraw") && worldJson["physicsDebugDraw"].is_boolean())
        out.physicsDebugDraw = worldJson["physicsDebugDraw"].get<bool>();

    if (worldJson.contains("physicsMode"))
        out.physicsMode = read_physics_mode(worldJson);
}

} // namespace ArtCade::ProjectJson
