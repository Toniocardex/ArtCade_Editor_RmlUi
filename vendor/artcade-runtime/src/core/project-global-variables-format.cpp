#include "project-global-variables-format.h"

#include <cmath>
#include <unordered_set>

namespace ArtCade::ProjectJson {
namespace {

constexpr std::size_t kMaxGameVariableKeyLength = 64;

bool is_valid_game_variable_key_first_char(char character)
{
    return (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z') || character == '_';
}

bool is_valid_game_variable_key_char(char character)
{
    return is_valid_game_variable_key_first_char(character)
        || (character >= '0' && character <= '9') || character == '.' || character == '-';
}

bool variable_value_matches_type(const GameVariableValue &value,
                                 GameVariableDefinition::Type type)
{
    switch (type) {
    case GameVariableDefinition::Type::Number:
        return std::holds_alternative<double>(value)
            && std::isfinite(std::get<double>(value));
    case GameVariableDefinition::Type::Boolean:
        return std::holds_alternative<bool>(value);
    case GameVariableDefinition::Type::String:
        return std::holds_alternative<std::string>(value);
    }
    return false;
}

} // namespace

bool is_valid_game_variable_key(const std::string &key, std::string &error_message)
{
    if (key.empty()) {
        error_message = "Variable key cannot be empty.";
        return false;
    }
    if (key.size() > kMaxGameVariableKeyLength) {
        error_message = "Variable key must be at most 64 characters.";
        return false;
    }
    if (!is_valid_game_variable_key_first_char(key.front())) {
        error_message = "Variable key must start with a letter or underscore.";
        return false;
    }
    for (const char character : key) {
        if (!is_valid_game_variable_key_char(character)) {
            error_message = "Variable key may contain only letters, digits, underscore, dot, and hyphen.";
            return false;
        }
    }
    return true;
}

bool validate_current_global_variables_document(const std::vector<GameVariableDefinition> &variables,
                                                std::string &error_message)
{
    std::unordered_set<GameVariableId> keys;
    for (const GameVariableDefinition &definition : variables) {
        if (!is_valid_game_variable_key(definition.key, error_message)) {
            return false;
        }
        if (!keys.insert(definition.key).second) {
            error_message = "Project contains duplicate global variable key \""
                            + definition.key + "\".";
            return false;
        }
        if (!variable_value_matches_type(definition.initialValue, definition.type)) {
            error_message = "Global variable \"" + definition.key
                            + "\" has an initialValue incompatible with its type.";
            return false;
        }
    }
    return true;
}

bool validate_current_global_variables_json(const nlohmann::json &raw,
                                            std::string &error_message)
{
    if (!raw.is_array()) {
        error_message = "Project requires globalVariables to be an array.";
        return false;
    }

    std::vector<GameVariableDefinition> definitions;
    definitions.reserve(raw.size());
    for (const nlohmann::json &item : raw) {
        if (!item.is_object() || !item.contains("key") || !item["key"].is_string()
            || !item.contains("type") || !item["type"].is_string()
            || !item.contains("initialValue")) {
            error_message = "Each global variable requires key, type, and initialValue.";
            return false;
        }
        if (item.contains("description") && !item["description"].is_string()) {
            error_message = "Global variable description must be a string.";
            return false;
        }
        for (auto field = item.begin(); field != item.end(); ++field) {
            const std::string &name = field.key();
            if (name != "key" && name != "type" && name != "initialValue"
                && name != "description") {
                error_message = "Global variable contains unsupported field \"" + name + "\".";
                return false;
            }
        }

        GameVariableDefinition definition;
        definition.key = item["key"].get<std::string>();
        const std::string type = item["type"].get<std::string>();
        if (type == "number" && item["initialValue"].is_number()) {
            const double value = item["initialValue"].get<double>();
            if (!std::isfinite(value)) {
                error_message = "Number variable initialValue must be finite.";
                return false;
            }
            definition.type = GameVariableDefinition::Type::Number;
            definition.initialValue = value;
        } else if (type == "boolean" && item["initialValue"].is_boolean()) {
            definition.type = GameVariableDefinition::Type::Boolean;
            definition.initialValue = item["initialValue"].get<bool>();
        } else if (type == "string" && item["initialValue"].is_string()) {
            definition.type = GameVariableDefinition::Type::String;
            definition.initialValue = item["initialValue"].get<std::string>();
        } else {
            error_message = "Global variable type and initialValue must agree.";
            return false;
        }
        if (item.contains("description")) {
            definition.description = item["description"].get<std::string>();
        }
        definitions.push_back(std::move(definition));
    }
    return validate_current_global_variables_document(definitions, error_message);
}

} // namespace ArtCade::ProjectJson
