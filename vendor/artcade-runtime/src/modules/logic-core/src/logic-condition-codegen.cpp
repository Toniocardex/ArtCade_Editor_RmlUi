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
    } else if (condition.typeId == kIsVisible) {
        const LogicPropertyDef* property = findProperty(condition, "expected");
        const bool expected = property ? std::get<bool>(property->value) : true;
        expression << "context.self:is_visible() == " << (expected ? "true" : "false");
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
        const double value = valueProperty ? std::get<double>(valueProperty->value) : 0.0;
        expression << "context:state_compare_number(\""
                   << escapeLua(key ? key->id : std::string{}) << "\", \""
                   << escapeLua(comparison ? comparison->value : std::string{"=="})
                   << "\", " << value << ")";
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
