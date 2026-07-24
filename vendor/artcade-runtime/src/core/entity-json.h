#pragma once
// =============================================================================
// entity-json — deserialize EntityDef fields from editor project JSON
// =============================================================================

#include "types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace ArtCade::ProjectJson {

/**
 * Reads transform, sprite, physics, and all optional gameplay components.
 * Does not modify entity identity fields (id, name, className, tags).
 */
void read_entity_components(const nlohmann::json& entityJson, EntityDef& out);
void read_variable_definitions(const nlohmann::json& raw,
                               std::vector<GameVariableDefinition>& out);

/**
 * Parses a scene entity instance.
 * @param use_entity_name_fallback  Assigns Entity_<id> when callers explicitly request it.
 */
void read_entity_instance(const nlohmann::json& entityJson,
                          EntityId fallbackId,
                          EntityDef& out,
                          bool use_entity_name_fallback);

/**
 * Parses an objectTypes prototype (id = 0).
 */
void read_object_type(const nlohmann::json& typeJson,
                      const std::string& mapKey,
                      EntityDef& out);

/** Parses entities array or id-keyed object map. */
void read_entities_map(const nlohmann::json& doc,
                       std::unordered_map<EntityId, EntityDef>& out,
                       bool use_entity_name_fallback);

/** Parses the objectTypes object map. */
void read_object_types_map(const nlohmann::json& doc,
                            std::unordered_map<std::string, EntityDef>& out);

/**
 * Reads objectTypes[].logicBoard for object types already present in
 * `objectTypes` (call after read_object_types_map). Shared by
 * AssetLoader::parseProjectJson and the editor's canonical project reader
 * (RU-01) - was previously duplicated inline in asset-loader.cpp.
 * @return false (leaving `objectTypes` partially updated) if a logicBoard
 * references an unknown object type, fails to parse, or fails board
 * validation; `error`, if non-null, receives a human-readable reason.
 */
bool read_object_type_logic_boards(const nlohmann::json& doc,
                                   std::unordered_map<std::string, EntityDef>& objectTypes,
                                   std::string* error = nullptr);

} // namespace ArtCade::ProjectJson
