#include "scene-json.h"

#include "json-primitives.h"
#include "project-defaults.h"
#include "project-meta-json.h"

namespace ArtCade::ProjectJson {

namespace {

constexpr Vec2 kDefaultWorldSize{
    ProjectDefaults::kSceneWorldWidth,
    ProjectDefaults::kSceneWorldHeight,
};
constexpr Vec2 kDefaultViewportSize{
    ProjectDefaults::kSceneViewportWidth,
    ProjectDefaults::kSceneViewportHeight,
};

void read_entity_id_list(const nlohmann::json& sceneJson, std::vector<EntityId>& out) {
    const nlohmann::json* eids = sceneJson.contains("entityIds")
        ? &sceneJson["entityIds"] : nullptr;
    if (eids == nullptr || !eids->is_array())
        return;
    out.clear();
    out.reserve(eids->size());
    for (const auto& id : *eids)
        out.push_back(id.get<EntityId>());
}

} // namespace

bool read_scene_instance(const nlohmann::json& instanceJson, SceneInstanceDef& out) {
    if (!instanceJson.is_object())
        return false;

    out = SceneInstanceDef{};
    out.id = instanceJson.value("id", 0u);
    out.objectTypeId = instanceJson.value("objectTypeId", std::string{});
    out.instanceName = instanceJson.value("instanceName", std::string{});
    if (instanceJson.contains("transform"))
        out.transform = read_transform(instanceJson["transform"]);
    if (instanceJson.contains("visible") && instanceJson["visible"].is_boolean())
        out.visible = instanceJson["visible"].get<bool>();
    out.layerId = instanceJson.value("layerId", std::string{});
    if (instanceJson.contains("spritePresentationOverride")
        && instanceJson["spritePresentationOverride"].is_object()) {
        const auto& value = instanceJson["spritePresentationOverride"];
        SpritePresentationOverride delta;
        if (value.contains("visible") && value["visible"].is_boolean()) {
            delta.visible = value["visible"].get<bool>();
        }
        if (value.contains("source") && value["source"].is_object()) {
            const auto& source = value["source"];
            const std::string kind = source.value("kind", std::string{"none"});
            if (kind == "image") {
                delta.source = SpritePresentationImage{
                    source.value("assetId", source.value("imageAssetId", std::string{}))};
            } else if (kind == "animation") {
                delta.source = SpritePresentationAnimation{
                    source.value("assetId", source.value("animationAssetId", std::string{})),
                    source.value("defaultClipId", source.value("initialClipId", std::string{})),
                    source.value("autoPlay", true), source.value("playbackSpeed", 1.f)};
            } else if (kind == "none") {
                delta.source = SpritePresentationNone{};
            }
        }
        out.spritePresentationOverride = std::move(delta);
    }
    if (instanceJson.contains("spriteRendererOverride")
        && instanceJson["spriteRendererOverride"].is_object()) {
        const auto& value = instanceJson["spriteRendererOverride"];
        SpriteRendererOverride delta;
        if (value.contains("imageAssetId"))
            delta.imageAssetId = value["imageAssetId"].get<std::string>();
        if (value.contains("visible")) delta.visible = value["visible"].get<bool>();
        if (value.contains("capabilityEnabled"))
            delta.capabilityEnabled = value["capabilityEnabled"].get<bool>();
        // Legacy: fold renderer.animationAssetId into animator override.
        if (value.contains("animationAssetId")) {
            SpriteAnimatorOverride animatorDelta =
                out.spriteAnimatorOverride.value_or(SpriteAnimatorOverride{});
            animatorDelta.animationAssetId = value["animationAssetId"].get<std::string>();
            out.spriteAnimatorOverride = std::move(animatorDelta);
        }
        out.spriteRendererOverride = std::move(delta);
    }
    if (instanceJson.contains("spriteAnimatorOverride")
        && instanceJson["spriteAnimatorOverride"].is_object()) {
        const auto& value = instanceJson["spriteAnimatorOverride"];
        SpriteAnimatorOverride delta = out.spriteAnimatorOverride.value_or(
            SpriteAnimatorOverride{});
        if (value.contains("animationAssetId"))
            delta.animationAssetId = value["animationAssetId"].get<std::string>();
        if (value.contains("defaultClipId"))
            delta.defaultClipId = value["defaultClipId"].get<std::string>();
        else if (value.contains("initialClipId"))
            delta.defaultClipId = value["initialClipId"].get<std::string>();
        if (value.contains("autoPlay")) delta.autoPlay = value["autoPlay"].get<bool>();
        if (value.contains("playbackSpeed"))
            delta.playbackSpeed = value["playbackSpeed"].get<float>();
        if (value.contains("capabilityEnabled"))
            delta.capabilityEnabled = value["capabilityEnabled"].get<bool>();
        out.spriteAnimatorOverride = std::move(delta);
    }
    if (instanceJson.contains("localVariableOverrides")
        && instanceJson["localVariableOverrides"].is_object()) {
        for (auto& [key, value] : instanceJson["localVariableOverrides"].items()) {
            if (value.is_number()) out.localVariableOverrides[key] = value.get<double>();
            else if (value.is_boolean()) out.localVariableOverrides[key] = value.get<bool>();
            else if (value.is_string()) out.localVariableOverrides[key] = value.get<std::string>();
        }
    }
    if (instanceJson.contains("tilemap") && instanceJson["tilemap"].is_object()) {
        const auto& tm = instanceJson["tilemap"];
        TilemapComponent component;
        component.tilesetAssetId = tm.value("tilesetAssetId", std::string{});
        if (tm.contains("cellSize"))
            component.cellSize = read_vec2(tm["cellSize"], component.cellSize);
        component.chunkSize = tm.value("chunkSize", component.chunkSize);
        if (tm.contains("chunks") && tm["chunks"].is_array()) {
            for (const auto& chunkJson : tm["chunks"]) {
                if (!chunkJson.is_object()) continue;
                TilemapChunk chunk;
                chunk.chunkX = chunkJson.value("chunkX", 0);
                chunk.chunkY = chunkJson.value("chunkY", 0);
                if (chunkJson.contains("cells") && chunkJson["cells"].is_array()) {
                    chunk.cells.reserve(chunkJson["cells"].size());
                    for (const auto& cellJson : chunkJson["cells"]) {
                        if (cellJson.is_null()) {
                            chunk.cells.emplace_back(std::nullopt);
                            continue;
                        }
                        if (!cellJson.is_object() || !cellJson.contains("tileId")) {
                            chunk.cells.emplace_back(std::nullopt);
                            continue;
                        }
                        TilemapCellValue value;
                        value.tileId = cellJson.value("tileId", std::string{});
                        value.flags = static_cast<TileTransformFlags>(
                            cellJson.value("flags", 0));
                        chunk.cells.emplace_back(std::move(value));
                    }
                }
                component.chunks.push_back(std::move(chunk));
            }
        }
        if (!component.tilesetAssetId.empty()) out.tilemap = std::move(component);
    }
    if (instanceJson.contains("cameraTarget") && instanceJson["cameraTarget"].is_object()) {
        const auto& value = instanceJson["cameraTarget"];
        CameraTargetComponent component;
        component.offsetX = value.value("offsetX", component.offsetX);
        component.offsetY = value.value("offsetY", component.offsetY);
        component.followSpeed = value.value("followSpeed", component.followSpeed);
        out.cameraTarget = component;
    }

    return out.id != 0 && !out.objectTypeId.empty();
}

void read_tilemap_object(const nlohmann::json& tmJson, TilemapData& out) {
    if (!tmJson.is_object())
        return;

    TilemapData tilemap;
    if (tmJson.contains("tileSize"))
        tilemap.tileSize = tmJson["tileSize"].get<float>();
    tilemap.cols = tmJson.value("cols", 0);
    tilemap.rows = tmJson.value("rows", 0);
    if (tmJson.contains("data") && tmJson["data"].is_array())
        tilemap.data = tmJson["data"].get<std::vector<int>>();
    tilemap.tilesetAssetId = tmJson.value("tilesetAssetId", std::string{});
    tilemap.defaultTilesetAssetId = tmJson.value("defaultTilesetAssetId", std::string{});

    if (tmJson.contains("sourceIndices") && tmJson["sourceIndices"].is_array())
        tilemap.sourceIndices = tmJson["sourceIndices"].get<std::vector<int>>();

    const nlohmann::json* sourcesJson = nullptr;
    if (tmJson.contains("tilesetSources") && tmJson["tilesetSources"].is_array())
        sourcesJson = &tmJson["tilesetSources"];
    if (sourcesJson) {
        for (const auto& item : *sourcesJson) {
            if (!item.is_object()) continue;
            TilesetSourceRef ref;
            ref.tilesetAssetId = item.value("tilesetAssetId", std::string{});
            if (!ref.tilesetAssetId.empty())
                tilemap.tilesetSources.push_back(std::move(ref));
        }
    }

    out = std::move(tilemap);
}

void read_tilemap(const nlohmann::json& sceneJson, TilemapData& out) {
    if (!sceneJson.contains("tilemap") || !sceneJson["tilemap"].is_object())
        return;
    read_tilemap_object(sceneJson["tilemap"], out);
}

void read_tilemap_layers(const nlohmann::json& sceneJson,
                         std::unordered_map<std::string, TilemapData>& out) {
    const nlohmann::json* layersJson =
        sceneJson.contains("tilemapLayers") && sceneJson["tilemapLayers"].is_object()
        ? &sceneJson["tilemapLayers"] : nullptr;
    if (!layersJson)
        return;

    out.clear();
    for (auto& [layerId, layerJson] : layersJson->items()) {
        TilemapData layer;
        read_tilemap_object(layerJson, layer);
        if (layer.cols > 0 && layer.rows > 0)
            out.emplace(layerId, std::move(layer));
    }
}

void read_scene_layer_settings(const nlohmann::json& sceneJson,
                               std::unordered_map<std::string, SceneLayerSettings>& out) {
    const nlohmann::json* lsJson =
        sceneJson.contains("layerSettings") && sceneJson["layerSettings"].is_object()
        ? &sceneJson["layerSettings"] : nullptr;
    if (!lsJson)
        return;

    out.clear();
    for (auto& [layerId, item] : lsJson->items()) {
        if (!item.is_object())
            continue;
        SceneLayerSettings s;
        s.visible = item.value("visible", true);
        s.opacity = item.value("opacity", 1.f);
        if (s.opacity < 0.f) s.opacity = 0.f;
        if (s.opacity > 1.f) s.opacity = 1.f;
        if (item.contains("parallax") && item["parallax"].is_object()) {
            const auto& p = item["parallax"];
            s.parallax.x = p.value("x", 1.f);
            s.parallax.y = p.value("y", 1.f);
        }
        if (item.contains("background") && item["background"].is_object()) {
            const auto& b = item["background"];
            s.background.imageId = b.value("imageId", std::string{});
            s.background.tileX = b.value("tileX", true);
            s.background.tileY = b.value("tileY", true);
            s.background.scrollX = b.value("scrollX", 0.f);
            s.background.scrollY = b.value("scrollY", 0.f);
        }
        out.emplace(layerId, std::move(s));
    }
}

void read_scene_def(const nlohmann::json& sceneJson,
                    const SceneId& fallbackId,
                    SceneDef& out) {
    out = SceneDef{};
    out.id   = sceneJson.value("id", fallbackId);
    out.name = sceneJson.value("name", fallbackId);

    if (sceneJson.contains("worldSize"))
        out.worldSize = read_vec2(sceneJson["worldSize"], kDefaultWorldSize);

    if (sceneJson.contains("viewportSize"))
        out.viewportSize = read_vec2(sceneJson["viewportSize"], kDefaultViewportSize);

    if (sceneJson.contains("cameraStart"))
        out.cameraStart = read_vec2(sceneJson["cameraStart"], { 0.f, 0.f });

    if (sceneJson.contains("backgroundColor"))
        out.backgroundColor = read_vec4(sceneJson["backgroundColor"]);

    read_entity_id_list(sceneJson, out.entityIds);

    if (sceneJson.contains("instances") && sceneJson["instances"].is_array()) {
        for (const auto& item : sceneJson["instances"]) {
            SceneInstanceDef inst;
            if (read_scene_instance(item, inst))
                out.instances.push_back(std::move(inst));
        }
    }

    read_tilemap(sceneJson, out.tilemap);
    read_tilemap_layers(sceneJson, out.tilemapLayers);
    read_scene_layer_settings(sceneJson, out.layerSettings);
    read_scene_layer_stack(sceneJson, out);
}

void read_scene_layer_stack(const nlohmann::json& sceneJson, SceneDef& out) {
    read_scene_layers(sceneJson, out.layers);
    if (sceneJson.contains("defaultLayerId") && sceneJson["defaultLayerId"].is_string()) {
        out.defaultLayerId = sceneJson["defaultLayerId"].get<std::string>();
    } else {
        out.defaultLayerId.clear();
    }
}

void read_scenes_map(const nlohmann::json& doc,
                     std::unordered_map<SceneId, SceneDef>& out) {
    out.clear();
    if (!doc.contains("scenes"))
        return;

    const auto& scenesJson = doc["scenes"];
    if (scenesJson.is_object()) {
        for (auto& [key, val] : scenesJson.items()) {
            SceneDef scene;
            read_scene_def(val, key, scene);
            out[scene.id] = std::move(scene);
        }
    }
}

} // namespace ArtCade::ProjectJson
