#include "project-doc-parser.h"
#include "object-type-materialize.h"
#include "asset-json.h"
#include "entity-json.h"
#include "scene-json.h"
#include "project-meta-json.h"
#include "collision-json.h"

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <string>
#include <utility>

namespace ArtCade::ProjectDocParser {

using json = nlohmann::json;

EntityDef parseEntityDef(const json& j, EntityId fallbackId) {
    EntityDef e;
    ProjectJson::read_entity_instance(j, fallbackId, e, true);
    if (e.className.empty())
        e.className = "Unknown";
    return e;
}

SceneDef parseSceneDef(const json& j, const SceneId& fallbackId) {
    SceneDef s;
    ProjectJson::read_scene_def(j, fallbackId, s);
    return s;
}

TilesetAsset parseTilesetAsset(const json& j) {
    TilesetAsset t;
    ProjectJson::read_tileset_asset(j, {}, t);
    return t;
}

std::unordered_map<EntityId, EntityDef>
parseEntities(const json& doc) {
    std::unordered_map<EntityId, EntityDef> entities;
    ProjectJson::read_entities_map(doc, entities, true);
    for (auto& [_, entity] : entities) {
        if (entity.className.empty())
            entity.className = "Unknown";
    }
    return entities;
}

std::unordered_map<SceneId, SceneDef>
parseScenes(const json& doc) {
    std::unordered_map<SceneId, SceneDef> scenes;
    ProjectJson::read_scenes_map(doc, scenes);
    return scenes;
}

std::vector<TilesetAsset>
parseTilesets(const json& doc) {
    std::vector<TilesetAsset> tilesets;
    ProjectJson::read_tilesets(doc, tilesets);
    return tilesets;
}

std::vector<TilePaletteEntry>
parseTilePalette(const json& doc) {
    std::vector<TilePaletteEntry> out;
    ProjectJson::read_tile_palette(doc, out);
    return out;
}

std::unordered_map<std::string, EntityDef>
parseObjectTypes(const json& doc) {
    std::unordered_map<std::string, EntityDef> objectTypes;
    ProjectJson::read_object_types_map(doc, objectTypes);
    return objectTypes;
}

void materializeV2Project(
    std::unordered_map<EntityId, EntityDef>& entities,
    std::unordered_map<SceneId, SceneDef>& scenes,
    const std::unordered_map<std::string, EntityDef>& objectTypes)
{
    if (objectTypes.empty()) return;
    const bool hasInstances = std::any_of(
        scenes.begin(), scenes.end(),
        [](const auto& kv) { return !kv.second.instances.empty(); });
    if (!hasInstances && !entities.empty()) return;

    ProjectDoc doc;
    doc.objectTypes = objectTypes;
    doc.scenes      = scenes;
    doc.entities    = entities;
    materializeProjectEntities(doc);
    entities = std::move(doc.entities);
    scenes   = std::move(doc.scenes);
}

std::vector<ImageAssetDef> parseImageAssets(const json& doc) {
    std::vector<ImageAssetDef> out;
    ProjectJson::read_image_assets(doc, out);
    return out;
}

std::vector<SpriteAnimationAssetDef> parseSpriteAnimationAssets(const json& doc) {
    std::vector<SpriteAnimationAssetDef> out;
    ProjectJson::read_sprite_animation_assets(doc, out);
    return out;
}

std::vector<AudioAssetDef> parseAudioAssets(const json& doc) {
    std::vector<AudioAssetDef> out;
    ProjectJson::read_audio_assets(doc, out);
    return out;
}

ArtCade::ProjectRuntimeSettings parseRuntimeSettings(const json& doc) {
    ArtCade::ProjectRuntimeSettings settings;
    ProjectJson::read_runtime_settings(doc, settings);
    return settings;
}

std::vector<GameVariableDefinition> parseGlobalVariables(const json& doc) {
    ProjectDoc project;
    ProjectJson::read_global_variables(doc, project);
    return std::move(project.globalVariables);
}

std::vector<PhysicsLayerDef> parsePhysicsLayers(const json& doc) {
    std::vector<PhysicsLayerDef> out;
    ProjectJson::read_physics_layers(doc, out);
    return out;
}

std::unordered_map<std::string, CollisionProfileDef> parseCollisionProfiles(const json& doc) {
    std::unordered_map<std::string, CollisionProfileDef> out;
    ProjectJson::read_collision_profiles(doc, out);
    return out;
}

std::unordered_map<std::string, std::string> parseSpritePathToAssetId(const json& doc) {
    std::unordered_map<std::string, std::string> out;
    if (!doc.contains("assets") || !doc["assets"].is_object())
        return out;
    for (auto& [key, assetJson] : doc["assets"].items()) {
        if (!assetJson.is_object()) continue;
        const std::string path = assetJson.value("path", std::string{});
        const std::string id = assetJson.value("id", key);
        if (!path.empty())
            out[path] = id;
    }
    return out;
}

} // namespace ArtCade::ProjectDocParser

#endif // __EMSCRIPTEN__
