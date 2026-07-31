#include "logic-codegen-internal.h"

namespace ArtCade::Logic::CodegenInternal {
namespace {

std::string conditionExpression(const LogicBlockDef& condition,
                                std::set<std::string>& requiredFeatures)
{
    std::ostringstream expression;
    if (condition.typeId == kIsGrounded) {
        const LogicPropertyDef* property = findProperty(condition, "expected");
        const bool expected = property ? std::get<bool>(property->value) : true;
        expression << "context.self:is_grounded() == " << (expected ? "true" : "false");
    } else if (condition.typeId == kIsFalling) {
        const LogicPropertyDef* property = findProperty(condition, "expected");
        const bool expected = property ? std::get<bool>(property->value) : true;
        expression << "context.self:is_falling() == " << (expected ? "true" : "false");
    } else if (condition.typeId == kPlatformerMotionState) {
        const LogicPropertyDef* property = findProperty(condition, "state");
        const auto* state = property
            ? std::get_if<LogicStringValue>(&property->value)
            : nullptr;
        const std::string name = state && !state->value.empty() ? state->value : "Moving";
        expression << "context.self:platformer_state() == \"" << escapeLua(name) << "\"";
    } else if (condition.typeId == kIsBlockedByWall) {
        const LogicPropertyDef* property = findProperty(condition, "side");
        const auto* side = property
            ? std::get_if<LogicStringValue>(&property->value)
            : nullptr;
        const std::string name = side && !side->value.empty() ? side->value : "Either";
        expression << "context.self:is_blocked_by_wall(\"" << escapeLua(name) << "\")";
    } else if (condition.typeId == kIsVisible) {
        const LogicPropertyDef* property = findProperty(condition, "expected");
        const bool expected = property ? std::get<bool>(property->value) : true;
        expression << "context.self:is_visible() == " << (expected ? "true" : "false");
    } else if (condition.typeId == kOutsideSceneBounds) {
        const LogicPropertyDef* property = findProperty(condition, "margin");
        const auto* marginExpression = property
            ? std::get_if<NumberExpression>(&property->value)
            : nullptr;
        const double margin = marginExpression
            ? literalNumberValue(*marginExpression).value_or(0.0) : 0.0;
        expression << "context.self:is_outside_scene(" << margin << ")";
    } else if (condition.typeId == kOtherIsObjectType) {
        const LogicPropertyDef* property = findProperty(condition, "objectTypeId");
        const auto* type = property
            ? std::get_if<LogicStringValue>(&property->value)
            : nullptr;
        expression << "context:other_is_object_type(other, \""
                   << escapeLua(type ? type->value : std::string{}) << "\")";
    } else if (condition.typeId == kKeyDown) {
        const LogicPropertyDef* key = findProperty(condition, "key");
        expression << "context:is_key_down(\""
                   << logicKeyName(std::get<LogicKey>(key->value)) << "\")";
    } else if (condition.typeId == kStateCompare) {
        const LogicPropertyDef* keyProperty = findProperty(condition, "key");
        const LogicPropertyDef* operatorProperty = findProperty(condition, "op");
        const LogicPropertyDef* valueProperty = findProperty(condition, "value");
        const auto* key = keyProperty
            ? std::get_if<LogicVariableReference>(&keyProperty->value)
            : nullptr;
        const auto* comparison = operatorProperty
            ? std::get_if<LogicStringValue>(&operatorProperty->value)
            : nullptr;
        // ADR-0029: Number properties store a NumberExpression; a literal is
        // what this comparison emits, and a dynamic value is rejected upstream
        // by the LiteralOnly policy.
        const auto* valueExpression = valueProperty
            ? std::get_if<NumberExpression>(&valueProperty->value)
            : nullptr;
        const double value =
            valueExpression ? literalNumberValue(*valueExpression).value_or(0.0) : 0.0;
        expression << "context:state_compare_number(\""
                   << escapeLua(key ? key->id : std::string{}) << "\", \""
                   << escapeLua(comparison ? comparison->value : std::string{"=="})
                   << "\", " << value << ")";
    } else if (condition.typeId == kStateCompareBoolean) {
        const LogicPropertyDef* keyProperty = findProperty(condition, "key");
        const LogicPropertyDef* expectedProperty = findProperty(condition, "expected");
        const auto* key = keyProperty
            ? std::get_if<LogicVariableReference>(&keyProperty->value)
            : nullptr;
        const bool expected = expectedProperty ? std::get<bool>(expectedProperty->value) : true;
        expression << "context:state_compare_boolean(\""
                   << escapeLua(key ? key->id : std::string{}) << "\", "
                   << (expected ? "true" : "false") << ")";
    } else if (condition.typeId == kStateCompareString) {
        const LogicPropertyDef* keyProperty = findProperty(condition, "key");
        const LogicPropertyDef* operatorProperty = findProperty(condition, "op");
        const LogicPropertyDef* valueProperty = findProperty(condition, "value");
        const auto* key = keyProperty
            ? std::get_if<LogicVariableReference>(&keyProperty->value)
            : nullptr;
        const auto* comparison = operatorProperty
            ? std::get_if<LogicStringValue>(&operatorProperty->value)
            : nullptr;
        const auto* literal = valueProperty
            ? std::get_if<LogicStringValue>(&valueProperty->value)
            : nullptr;
        // All three are escapeLua()'d: value is an arbitrary author-typed
        // literal, not a numeric literal like Compare Number's.
        expression << "context:state_compare_string(\""
                   << escapeLua(key ? key->id : std::string{}) << "\", \""
                   << escapeLua(comparison ? comparison->value : std::string{"=="})
                   << "\", \""
                   << escapeLua(literal ? literal->value : std::string{}) << "\")";
    }
    if (const LogicBlockDescriptor* descriptor = findDescriptor(condition.typeId)) {
        if (!descriptor->requiredFeature.empty()) {
            requiredFeatures.insert(descriptor->requiredFeature);
        }
    }
    return expression.str();
}

} // namespace

std::string escapeLua(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char character : value) {
        switch (character) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += character; break;
        }
    }
    return out;
}

std::string emitConditionExpression(const LogicBlockDef& condition,
                                    std::set<std::string>& requiredFeatures)
{
    return conditionExpression(condition, requiredFeatures);
}

std::string emitConditionsExpression(const std::vector<LogicConditionClause>& conditions,
                                     std::set<std::string>& requiredFeatures)
{
    if (conditions.empty()) {
        return {};
    }

    std::vector<std::string> groups;
    std::ostringstream group;
    for (std::size_t index = 0; index < conditions.size(); ++index) {
        const LogicConditionClause& clause = conditions[index];
        if (index > 0 && clause.joinBefore == LogicConditionJoin::Or) {
            groups.push_back(group.str());
            group.str(std::string{});
            group.clear();
        } else if (index > 0) {
            group << " and ";
        }
        const std::string expression = conditionExpression(clause.block, requiredFeatures);
        group << (clause.negated ? "not (" + expression + ")" : expression);
    }
    groups.push_back(group.str());

    std::ostringstream expression;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (index > 0) {
            expression << " or ";
        }
        expression << "(" << groups[index] << ")";
    }
    return expression.str();
}

bool emitConditionGuard(std::ostringstream& lua,
                        const std::vector<LogicConditionClause>& conditions,
                        std::set<std::string>& requiredFeatures)
{
    const std::string expression = emitConditionsExpression(conditions, requiredFeatures);
    if (expression.empty()) {
        return false;
    }
    lua << "    if " << expression << " then\n";
    return true;
}

} // namespace ArtCade::Logic::CodegenInternal
