#pragma once
// =============================================================================
// scene-json — deserialize SceneDef fields from editor project JSON
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace ArtCade::ProjectJson {

/**
 * Parses a scene placement instance. Returns true when id and objectTypeId are valid.
 */
bool read_scene_instance(const nlohmann::json& instanceJson, SceneInstanceDef& out);

/** Parses tilemap object fields into @p out (unchanged when @p tmJson is not an object). */
void read_tilemap_object(const nlohmann::json& tmJson, TilemapData& out);

/** Parses scene.tilemap when present; leaves @p out unchanged when absent. */
void read_tilemap(const nlohmann::json& sceneJson, TilemapData& out);

/** Parses scene.tilemapLayers map (keyed by layer id) when present. */
void read_tilemap_layers(const nlohmann::json& sceneJson,
                         std::unordered_map<std::string, TilemapData>& out);

/** Parses scene.layerSettings (per-scene visual overrides keyed by layer id). */
void read_scene_layer_settings(const nlohmann::json& sceneJson,
                               std::unordered_map<std::string, SceneLayerSettings>& out);

/**
 * Parses scene.layers (+ defaultLayerId). Layers are required for a valid scene;
 * callers may normalize after parse.
 */
void read_scene_layer_stack(const nlohmann::json& sceneJson, SceneDef& out);

/**
 * Parses a single scene object (array entry or scenes-map value).
 * Identity defaults to @p fallbackId when id/name are omitted.
 */
void read_scene_def(const nlohmann::json& sceneJson,
                    const SceneId& fallbackId,
                    SceneDef& out);

/** Parses scenes array or id-keyed object map. */
void read_scenes_map(const nlohmann::json& doc,
                     std::unordered_map<SceneId, SceneDef>& out);

} // namespace ArtCade::ProjectJson
