#include "logic-number-expression-validation.h"

#include <cmath>

namespace ArtCade::Logic {
namespace {

struct CountState {
    std::size_t nodes = 0;
    std::size_t depth = 0;
    std::size_t maxDepth = 0;
};

void walk(const NumberExpression& expression, CountState& state,
          NumberExpressionValidationResult& result,
          const NumberExpressionContext& context);

bool fail(NumberExpressionValidationResult& result, const char* code, const char* message) {
    result = {false, code, message};
    return false;
}

bool checkNode(const NumberExpression& expression, CountState& state,
               NumberExpressionValidationResult& result,
               const NumberExpressionContext& context) {
    ++state.nodes;
    if (state.nodes > kMaximumNumberExpressionNodes)
        return fail(result, "NE_NODE_LIMIT", "Number expression exceeds node limit");
    ++state.depth;
    if (state.depth > state.maxDepth) state.maxDepth = state.depth;
    if (state.depth > kMaximumNumberExpressionDepth)
        return fail(result, "NE_DEPTH_LIMIT", "Number expression exceeds depth limit");

    const bool ok = std::visit([&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            if (!std::isfinite(node.value))
                return fail(result, "NE_NON_FINITE", "Number literal must be finite");
            return true;
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            switch (node.property) {
            case NumberProperty::SelfPositionX:
            case NumberProperty::SelfPositionY:
                if (!context.selfAvailable)
                    return fail(result, "NE_CONTEXT", "Self position is not available");
                break;
            case NumberProperty::SceneWorldWidth:
            case NumberProperty::SceneWorldHeight:
                if (!context.sceneAvailable)
                    return fail(result, "NE_CONTEXT", "Scene world size is not available");
                break;
            case NumberProperty::DeltaSeconds:
                if (!context.deltaSecondsAvailable)
                    return fail(result, "NE_CONTEXT",
                                "Delta Seconds is not available in this event context");
                break;
            }
            return true;
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            if (node.variableId.empty())
                return fail(result, "NE_VARIABLE", "Variable id is empty");
            const std::vector<GameVariableDefinition>* catalog =
                node.scope == NumberVariableScope::Global ? context.globalVariables
                                                          : context.localVariables;
            if (!catalog) {
                return fail(result, "NE_VARIABLE",
                            node.scope == NumberVariableScope::Global
                                ? "Global variable catalog is unavailable"
                                : "Local variable catalog is unavailable");
            }
            const GameVariableDefinition* def = nullptr;
            for (const GameVariableDefinition& candidate : *catalog) {
                if (candidate.key == node.variableId) {
                    def = &candidate;
                    break;
                }
            }
            if (!def) {
                return fail(result, "NE_VARIABLE",
                            node.scope == NumberVariableScope::Global
                                ? "Global variable is missing"
                                : "Local variable is missing");
            }
            if (def->type != GameVariableDefinition::Type::Number)
                return fail(result, "NE_VARIABLE", "Variable must be Number");
            return true;
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            if (!node.operand)
                return fail(result, "NE_INCOMPLETE", "Unary operand is missing");
            walk(*node.operand, state, result, context);
            return result.ok;
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            if (!node.left || !node.right)
                return fail(result, "NE_INCOMPLETE", "Binary operand is missing");
            if (node.operation == NumberBinaryOperator::Divide) {
                if (const auto denom = literalNumberValue(*node.right)) {
                    if (std::abs(*denom) <= kExpressionDivisionEpsilon)
                        return fail(result, "NE_DIVIDE_ZERO", "Literal division by zero");
                }
            }
            walk(*node.left, state, result, context);
            if (!result.ok) return false;
            walk(*node.right, state, result, context);
            return result.ok;
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            if (!node.value || !node.minimum || !node.maximum)
                return fail(result, "NE_INCOMPLETE", "Clamp child is missing");
            const auto minLit = literalNumberValue(*node.minimum);
            const auto maxLit = literalNumberValue(*node.maximum);
            if (minLit && maxLit && *minLit > *maxLit)
                return fail(result, "NE_CLAMP_ORDER",
                            "Clamp minimum must not be greater than maximum");
            walk(*node.value, state, result, context);
            if (!result.ok) return false;
            walk(*node.minimum, state, result, context);
            if (!result.ok) return false;
            walk(*node.maximum, state, result, context);
            return result.ok;
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            if (!node.from || !node.to || !node.amount)
                return fail(result, "NE_INCOMPLETE", "Lerp child is missing");
            walk(*node.from, state, result, context);
            if (!result.ok) return false;
            walk(*node.to, state, result, context);
            if (!result.ok) return false;
            walk(*node.amount, state, result, context);
            return result.ok;
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            if (!node.minimum || !node.maximum)
                return fail(result, "NE_INCOMPLETE", "Random Range child is missing");
            const auto minLit = literalNumberValue(*node.minimum);
            const auto maxLit = literalNumberValue(*node.maximum);
            if (minLit && maxLit && *minLit > *maxLit)
                return fail(result, "NE_RANDOM_ORDER",
                            "Random Range minimum must not be greater than maximum");
            walk(*node.minimum, state, result, context);
            if (!result.ok) return false;
            walk(*node.maximum, state, result, context);
            return result.ok;
        }
    }, expression.value());

    --state.depth;
    return ok;
}

void walk(const NumberExpression& expression, CountState& state,
          NumberExpressionValidationResult& result,
          const NumberExpressionContext& context) {
    if (!result.ok) return;
    if (!checkNode(expression, state, result, context)) return;
    result.ok = true;
}

} // namespace

NumberExpressionValidationResult validateNumberExpression(
    const NumberExpression& expression,
    const NumberExpressionContext& context) {
    NumberExpressionValidationResult result{true, {}, {}};
    CountState state;
    walk(expression, state, result, context);
    return result;
}

NumberExpressionValidationResult requireLiteralNumberExpression(
    const NumberExpression& expression) {
    if (!isLiteralNumberExpression(expression)) {
        return {false, "NE_LITERAL_ONLY",
                "Dynamic expressions are not enabled for this property"};
    }
    const auto value = literalNumberValue(expression);
    if (!value || !std::isfinite(*value)) {
        return {false, "NE_NON_FINITE", "Number literal must be finite"};
    }
    return {true, {}, {}};
}

} // namespace ArtCade::Logic
