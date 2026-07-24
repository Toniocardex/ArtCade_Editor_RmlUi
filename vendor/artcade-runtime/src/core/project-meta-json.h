#pragma once
// =============================================================================
// project-meta-json — world, tile palette, tilesets, thumbnails from project JSON
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::ProjectJson {

/** "#RRGGBB" / "#RGB" → Vec4 (0..1). Opaque grey on invalid input. */
Vec4 hex_to_vec4(const std::string& hex);

/** Parses world object fields into @p out (defaults applied when keys are absent). */
void read_world_settings(const nlohmann::json& worldJson, WorldSettings& out);

/** Parses tilePalette / tile_palette array entries with id >= 1. */
void read_tile_palette(const nlohmann::json& doc, std::vector<TilePaletteEntry>& out);

/**
 * Parses one tileset record. @p mapKey is used when assetId is omitted (object map).
 */
void read_tileset_asset(const nlohmann::json& tilesetJson,
                        const std::string& mapKey,
                        TilesetAsset& out);

/** Parses tilesets array or object map. */
void read_tilesets(const nlohmann::json& doc, std::vector<TilesetAsset>& out);

/** Parses thumbnails object map (sceneId → relative path). */
void read_thumbnails(const nlohmann::json& doc,
                     std::unordered_map<SceneId, std::string>& out);

/** Parses canonical current-format project.json identity and routing fields. */
void read_project_header(const nlohmann::json& doc, ProjectDoc& out);
/**
 * Reads globalVariables from a document already accepted by
 * validate_current_project_json(). Invalid or stale input is not accepted here.
 */
void read_global_variables(const nlohmann::json& doc, ProjectDoc& out);

/**
 * Partial runtime overlay for editor preview: only keys present in @p doc are applied.
 * Differs from read_world_settings, which applies defaults when the world block exists.
 */
void read_runtime_settings(const nlohmann::json& doc, ProjectRuntimeSettings& out);

/** Parses a layers array from a scene JSON object (not ProjectDoc root). */
void read_scene_layers(const nlohmann::json& container, std::vector<SceneLayerDef>& out);

} // namespace ArtCade::ProjectJson
