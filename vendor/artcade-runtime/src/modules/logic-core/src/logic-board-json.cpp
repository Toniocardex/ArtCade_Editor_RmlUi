#include "../include/logic-core.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <unordered_set>

namespace ArtCade::Logic {
namespace {

nlohmann::json valueToJson(const LogicValue& value) {
    if (const bool* v = std::get_if<bool>(&value)) return {{"kind", "bool"}, {"value", *v}};
    if (const int64_t* v = std::get_if<int64_t>(&value)) return {{"kind", "integer"}, {"value", *v}};
    if (const double* v = std::get_if<double>(&value)) return {{"kind", "number"}, {"value", *v}};
    if (const LogicStringValue* v = std::get_if<LogicStringValue>(&value))
        return {{"kind", "string"}, {"value", v->value}};
    if (const Vec2* v = std::get_if<Vec2>(&value))
        return {{"kind", "vec2"}, {"x", v->x}, {"y", v->y}};
    if (const LogicAssetReference* v = std::get_if<LogicAssetReference>(&value))
        return {{"kind", "asset"}, {"id", v->id}};
    if (std::holds_alternative<LogicEntityReference>(value))
        return {{"kind", "entity"}, {"target", "self"}};
    if (const LogicVariableReference* v = std::get_if<LogicVariableReference>(&value))
        return {{"kind", "variable"}, {"id", v->id}};
    return {{"kind", "key"}, {"value", logicKeyName(std::get<LogicKey>(value))}};
}

bool readString(const nlohmann::json& json, const char* key, std::string& out) {
    if (!json.contains(key) || !json[key].is_string()) return false;
    out = json[key].get<std::string>();
    return true;
}

bool valueFromJson(const nlohmann::json& json, LogicValue& out, std::string& error) {
    if (!json.is_object()) { error = "Logic property value must be an object"; return false; }
    std::string kind;
    if (!readString(json, "kind", kind)) { error = "Logic property value kind is missing"; return false; }
    if (kind == "bool" && json.contains("value") && json["value"].is_boolean()) {
        out = json["value"].get<bool>(); return true;
    }
    if (kind == "integer" && json.contains("value") && json["value"].is_number_integer()) {
        out = json["value"].get<int64_t>(); return true;
    }
    if (kind == "number" && json.contains("value") && json["value"].is_number()) {
        out = json["value"].get<double>(); return true;
    }
    if (kind == "string" && json.contains("value") && json["value"].is_string()) {
        out = LogicStringValue{json["value"].get<std::string>()}; return true;
    }
    if (kind == "vec2" && json.contains("x") && json.contains("y")
        && json["x"].is_number() && json["y"].is_number()) {
        out = Vec2{json["x"].get<float>(), json["y"].get<float>()}; return true;
    }
    if (kind == "asset") {
        std::string id;
        if (readString(json, "id", id)) { out = LogicAssetReference{std::move(id)}; return true; }
    }
    if (kind == "entity") {
        std::string target;
        if (readString(json, "target", target) && target == "self") {
            out = LogicEntityReference{}; return true;
        }
    }
    if (kind == "variable") {
        std::string id;
        if (readString(json, "id", id)) { out = LogicVariableReference{std::move(id)}; return true; }
    }
    if (kind == "key" && json.contains("value") && json["value"].is_string()) {
        const auto key = logicKeyFromName(json["value"].get<std::string>());
        if (key) { out = *key; return true; }
    }
    error = "Logic property value is invalid or unsupported";
    return false;
}

LogicValueKind valueKind(const LogicValue& value) {
    if (std::holds_alternative<bool>(value)) return LogicValueKind::Bool;
    if (std::holds_alternative<int64_t>(value)) return LogicValueKind::Integer;
    if (std::holds_alternative<double>(value)) return LogicValueKind::Number;
    if (std::holds_alternative<LogicStringValue>(value)) return LogicValueKind::String;
    if (std::holds_alternative<Vec2>(value)) return LogicValueKind::Vec2;
    if (std::holds_alternative<LogicAssetReference>(value)) return LogicValueKind::Asset;
    if (std::holds_alternative<LogicEntityReference>(value)) return LogicValueKind::Entity;
    if (std::holds_alternative<LogicVariableReference>(value)) return LogicValueKind::Variable;
    return LogicValueKind::Key;
}

nlohmann::json blockToJson(const LogicBlockDef& block) {
    nlohmann::json properties = nlohmann::json::array();
    for (const LogicPropertyDef& property : block.properties)
        properties.push_back({{"key", property.key}, {"value", valueToJson(property.value)}});
    return {{"typeId", block.typeId}, {"properties", std::move(properties)}};
}

bool blockFromJson(const nlohmann::json& json, LogicBlockDef& out, std::string& error) {
    if (!json.is_object() || !readString(json, "typeId", out.typeId)) {
        error = "Logic block typeId is missing"; return false;
    }
    if (!json.contains("properties") || !json["properties"].is_array()) {
        error = "Logic block properties must be an array"; return false;
    }
    const LogicBlockDescriptor* descriptor = findDescriptor(out.typeId);
    out.properties.clear();
    std::unordered_set<std::string> seen;
    for (const auto& item : json["properties"]) {
        LogicPropertyDef property;
        if (!item.is_object() || !readString(item, "key", property.key)
            || !item.contains("value")) {
            error = "Logic property is malformed"; return false;
        }
        if (!seen.insert(property.key).second) {
            error = "Duplicate Logic property: " + property.key; return false;
        }
        if (!valueFromJson(item["value"], property.value, error)) return false;
        // ADR-0013: unknown catalog typeIds stay loadable for repair. Property
        // shape is accepted as stored; AuthoringDiagnostics / Executable flag it.
        if (descriptor) {
            const auto descriptor_property = std::find_if(
                descriptor->properties.begin(), descriptor->properties.end(),
                [&](const LogicPropertyDescriptor& candidate) {
                    return candidate.key == property.key;
                });
            if (descriptor_property == descriptor->properties.end()) {
                // ADR-0012 legacy Move Horizontal may still store numeric axis.
                if (!(out.typeId == kMoveHorizontal && property.key == "axis"
                      && std::holds_alternative<double>(property.value))) {
                    error = "Unknown Logic property: " + property.key; return false;
                }
            } else if (valueKind(property.value) != descriptor_property->valueKind) {
                error = "Logic property has the wrong value type: " + property.key;
                return false;
            }
        }
        out.properties.push_back(std::move(property));
    }
    return true;
}

} // namespace

nlohmann::json logicBoardToJson(const LogicBoardDef& board) {
    nlohmann::json rules = nlohmann::json::array();
    for (const LogicRuleDef& rule : board.rules) {
        nlohmann::json conditions = nlohmann::json::array();
        for (std::size_t index = 0; index < rule.conditions.size(); ++index) {
            const LogicConditionClause& clause = rule.conditions[index];
            if (index == 0 && clause.joinBefore != LogicConditionJoin::And) {
                throw std::logic_error("First Logic condition must use AND");
            }
            const char* join = nullptr;
            switch (clause.joinBefore) {
            case LogicConditionJoin::And:
                join = "and";
                break;
            case LogicConditionJoin::Or:
                join = "or";
                break;
            default:
                throw std::logic_error("Unknown Logic condition join operator");
            }
            conditions.push_back({
                {"join", join},
                {"negated", clause.negated},
                {"block", blockToJson(clause.block)},
            });
        }
        nlohmann::json actions = nlohmann::json::array();
        for (const LogicBlockDef& block : rule.actions) actions.push_back(blockToJson(block));
        nlohmann::json ruleJson = {
            {"id", rule.id}, {"enabled", rule.enabled},
            {"trigger", blockToJson(rule.trigger)},
            {"conditions", std::move(conditions)},
            {"actions", std::move(actions)},
        };
        ruleJson["name"] = rule.name;
        // Default EveryOccurrence may be omitted by older writers; always emit
        // the token so round-trips stay explicit and readable.
        ruleJson["executionMode"] = logicExecutionModeToString(rule.executionMode);
        // Empty display grouping metadata is omitted from the current format.
        if (!rule.sectionId.empty()) ruleJson["sectionId"] = rule.sectionId;
        rules.push_back(std::move(ruleJson));
    }
    nlohmann::json boardJson = {
        {"id", board.id},
        {"schemaVersion", board.schemaVersion},
        {"apiVersion", board.apiVersion},
        {"rules", std::move(rules)},
    };
    if (!board.sections.empty()) {
        nlohmann::json sections = nlohmann::json::array();
        for (const LogicSectionDef& section : board.sections)
            sections.push_back({{"id", section.id}, {"name", section.name}});
        boardJson["sections"] = std::move(sections);
    }
    return boardJson;
}

LogicJsonResult logicBoardFromJson(const nlohmann::json& json, LogicBoardDef& out) {
    try {
        if (!json.is_object()) return {false, "Logic Board must be an object"};
        LogicBoardDef parsed;
        if (!readString(json, "id", parsed.id)) return {false, "Logic Board id is missing"};
        if (!json.contains("schemaVersion") || !json["schemaVersion"].is_number_unsigned())
            return {false, "Logic Board schemaVersion is invalid"};
        if (!json.contains("apiVersion") || !json["apiVersion"].is_number_unsigned())
            return {false, "Logic Board apiVersion is invalid"};
        parsed.schemaVersion = json["schemaVersion"].get<uint32_t>();
        parsed.apiVersion = json["apiVersion"].get<uint32_t>();
        if (parsed.schemaVersion != kLogicBoardSchemaVersion) {
            return {false, "Unsupported Logic Board schemaVersion"};
        }
        if (parsed.apiVersion != kLogicApiVersion) {
            return {false, "Unsupported Logic Board apiVersion"};
        }
        if (!json.contains("rules") || !json["rules"].is_array())
            return {false, "Logic Board rules must be an array"};

        if (json.contains("sections")) {
            if (!json["sections"].is_array())
                return {false, "Logic Board sections must be an array"};
            std::unordered_set<std::string> sectionIds;
            for (const auto& item : json["sections"]) {
                if (!item.is_object()) return {false, "Logic section must be an object"};
                LogicSectionDef section;
                if (!readString(item, "id", section.id))
                    return {false, "Logic section id is missing"};
                if (!readString(item, "name", section.name))
                    return {false, "Logic section name is missing"};
                if (!sectionIds.insert(section.id).second)
                    return {false, "Duplicate Logic section id"};
                parsed.sections.push_back(std::move(section));
            }
            if (parsed.sections.size() > kMaxSectionsPerBoard)
                return {false, "Logic Board section limit exceeded"};
        }

        std::unordered_set<std::string> ruleIds;
        for (const auto& item : json["rules"]) {
            if (!item.is_object()) return {false, "Logic rule must be an object"};
            LogicRuleDef rule;
            if (!readString(item, "id", rule.id)) return {false, "Logic rule id is missing"};
            if (!ruleIds.insert(rule.id).second) return {false, "Duplicate Logic rule id"};
            if (!readString(item, "name", rule.name) || rule.name.empty())
                return {false, "Logic rule name is missing or empty"};
            if (!item.contains("enabled") || !item["enabled"].is_boolean())
                return {false, "Logic rule enabled is invalid"};
            rule.enabled = item["enabled"].get<bool>();
            rule.executionMode = LogicExecutionMode::EveryOccurrence;
            if (item.contains("executionMode")) {
                if (!item["executionMode"].is_string())
                    return {false, "Logic rule executionMode is invalid"};
                const auto mode = logicExecutionModeFromString(
                    item["executionMode"].get<std::string>());
                if (!mode) return {false, "Unknown Logic rule executionMode"};
                rule.executionMode = *mode;
            }
            if (item.contains("sectionId")) {
                if (!item["sectionId"].is_string())
                    return {false, "Logic rule sectionId is invalid"};
                rule.sectionId = item["sectionId"].get<std::string>();
            }
            std::string error;
            if (!item.contains("trigger") || !blockFromJson(item["trigger"], rule.trigger, error))
                return {false, error.empty() ? "Logic rule trigger is missing" : error};
            if (!item.contains("conditions") || !item["conditions"].is_array())
                return {false, "Logic rule conditions must be an array"};
            if (!item.contains("actions") || !item["actions"].is_array())
                return {false, "Logic rule actions must be an array"};
            for (const auto& raw : item["conditions"]) {
                if (!raw.is_object() || raw.size() != 3
                    || !raw.contains("join") || !raw["join"].is_string()
                    || !raw.contains("negated") || !raw["negated"].is_boolean()
                    || !raw.contains("block")) {
                    return {false, "Logic condition clause is invalid"};
                }
                LogicConditionClause clause;
                const std::string join = raw["join"].get<std::string>();
                if (join == "and") clause.joinBefore = LogicConditionJoin::And;
                else if (join == "or") clause.joinBefore = LogicConditionJoin::Or;
                else return {false, "Unknown Logic condition join operator"};
                clause.negated = raw["negated"].get<bool>();
                if (!blockFromJson(raw["block"], clause.block, error)) return {false, error};
                rule.conditions.push_back(std::move(clause));
            }
            if (!rule.conditions.empty()
                && rule.conditions.front().joinBefore != LogicConditionJoin::And) {
                return {false, "First Logic condition must use AND"};
            }
            for (const auto& raw : item["actions"]) {
                LogicBlockDef block;
                if (!blockFromJson(raw, block, error)) return {false, error};
                rule.actions.push_back(std::move(block));
            }
            parsed.rules.push_back(std::move(rule));
        }
        // ADR-0016: rewrite Is Falling (expected true/default) → Platformer State Falling.
        // expected == false stays as-is (ambiguous vs other states; repair manually).
        auto migrateIsFallingBlock = [](LogicBlockDef& block) {
            if (block.typeId != kIsFalling) return;
            const LogicPropertyDef* expected = findProperty(block, "expected");
            const bool wantFalling = !expected || std::get<bool>(expected->value);
            if (!wantFalling) return;
            block = makeDefaultEventBlock(kPlatformerMotionState);
            for (LogicPropertyDef& property : block.properties) {
                if (property.key == "state") property.value = LogicStringValue{"Falling"};
            }
        };
        for (LogicRuleDef& rule : parsed.rules) {
            migrateIsFallingBlock(rule.trigger);
            for (LogicConditionClause& clause : rule.conditions)
                migrateIsFallingBlock(clause.block);
        }
        out = std::move(parsed);
        return {true, {}};
    } catch (const std::exception& e) {
        return {false, std::string("Logic Board JSON error: ") + e.what()};
    }
}

} // namespace ArtCade::Logic

