#pragma once

#include "types.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ArtCade::ProjectJson {

/**
 * Validates a stable ProjectDoc global-variable identifier.
 * @param key           candidate identifier.
 * @param error_message receives a user-facing rejection reason on failure.
 * @returns true for [A-Za-z_][A-Za-z0-9_.-]{0,63}.
 */
bool is_valid_game_variable_key(const std::string &key, std::string &error_message);

/**
 * Validates current-format persisted global variables before serialization.
 * @param variables     ProjectDoc definitions to validate.
 * @param error_message receives the first validation error.
 * @returns true only when keys are unique and each initial value matches its type.
 */
bool validate_current_global_variables_document(const std::vector<GameVariableDefinition> &variables,
                                                std::string &error_message);

/**
 * Validates the exact JSON representation of current-format global variables.
 * @param raw           decoded globalVariables array.
 * @param error_message receives the first validation error.
 * @returns true only for the current schema without unsupported fields.
 */
bool validate_current_global_variables_json(const nlohmann::json &raw,
                                            std::string &error_message);

} // namespace ArtCade::ProjectJson
