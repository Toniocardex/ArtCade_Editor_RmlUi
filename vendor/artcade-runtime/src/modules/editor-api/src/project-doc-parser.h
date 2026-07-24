#pragma once
// =============================================================================
// project-doc-parser  -- JSON -> EntityDef / SceneDef / TilesetAsset
// =============================================================================
//
// Extracted from editor-api.cpp during the Phase 5 split (see
// docs/TECHNICAL_DEBT_REVIEW.md). editor_load_project() builds a project
// from a JSON blob; keeping the parsing helpers separate makes the
// command itself short and the parsing surface easy to unit-test in
// isolation.
//
// WASM-only: the parser is part of the editor bridge, which only links in
// the Emscripten build.
// =============================================================================

#ifdef __EMSCRIPTEN__

#include "../../../core/types.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::ProjectDocParser {

ArtCade::EntityDef       parseEntityDef (const nlohmann::json& j,
                                         ArtCade::EntityId fallbackId);
ArtCade::SceneDef        parseSceneDef  (const nlohmann::json& j,
                                         const ArtCade::SceneId& fallbackId);
ArtCade::TilesetAsset    parseTilesetAsset(const nlohmann::json& j);

/**
 * Top-level helpers for editor_load_project().
 *
 *  - parseEntities() and parseScenes() accept either a JSON array or an
 *    object keyed by id.
 *  - parseTilesets() accepts the same shape.
 */
std::unordered_map<ArtCade::EntityId, ArtCade::EntityDef>
parseEntities(const nlohmann::json& doc);

std::unordered_map<ArtCade::SceneId, ArtCade::SceneDef>
parseScenes(const nlohmann::json& doc);

std::vector<ArtCade::TilesetAsset>
parseTilesets(const nlohmann::json& doc);

std::vector<ArtCade::TilePaletteEntry>
parseTilePalette(const nlohmann::json& doc);

std::unordered_map<std::string, ArtCade::EntityDef>
parseObjectTypes(const nlohmann::json& doc);

/** Merge objectTypes + scene.instances when v2 project has no flat entities map. */
void materializeV2Project(
    std::unordered_map<ArtCade::EntityId, ArtCade::EntityDef>& entities,
    std::unordered_map<ArtCade::SceneId, ArtCade::SceneDef>& scenes,
    const std::unordered_map<std::string, ArtCade::EntityDef>& objectTypes);

/** targetFPS + world runtime fields from editor_load_project JSON. */
ArtCade::ProjectRuntimeSettings parseRuntimeSettings(const nlohmann::json& doc);

std::vector<ArtCade::GameVariableDefinition>
parseGlobalVariables(const nlohmann::json& doc);

std::vector<ArtCade::ImageAssetDef>
parseImageAssets(const nlohmann::json& doc);

std::vector<ArtCade::SpriteAnimationAssetDef>
parseSpriteAnimationAssets(const nlohmann::json& doc);

std::vector<ArtCade::AudioAssetDef>
parseAudioAssets(const nlohmann::json& doc);

std::vector<ArtCade::PhysicsLayerDef>
parsePhysicsLayers(const nlohmann::json& doc);

std::unordered_map<std::string, ArtCade::CollisionProfileDef>
parseCollisionProfiles(const nlohmann::json& doc);

/** Maps sprite.spriteAssetId path -> library asset id for collision profile lookup. */
std::unordered_map<std::string, std::string>
parseSpritePathToAssetId(const nlohmann::json& doc);

} // namespace ArtCade::ProjectDocParser

#endif // __EMSCRIPTEN__
