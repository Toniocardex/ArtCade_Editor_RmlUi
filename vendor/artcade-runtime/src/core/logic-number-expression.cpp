#include "logic-number-expression.h"

#include "types.h"

#include <cmath>
#include <type_traits>

namespace ArtCade {
namespace {

NumberExpressionBox cloneBox(const NumberExpressionBox& box) {
    if (!box) return nullptr;
    return boxNumberExpression(cloneNumberExpression(*box));
}

NumberExpressionVariant cloneVariant(const NumberExpressionVariant& value) {
    return std::visit([](const auto& node) -> NumberExpressionVariant {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>
                      || std::is_same_v<T, NumberPropertyExpression>
                      || std::is_same_v<T, NumberVariableExpression>) {
            return node;
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            NumberUnaryExpression copy;
            copy.operation = node.operation;
            copy.operand = cloneBox(node.operand);
            return copy;
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            NumberBinaryExpression copy;
            copy.operation = node.operation;
            copy.left = cloneBox(node.left);
            copy.right = cloneBox(node.right);
            return copy;
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            NumberClampExpression copy;
            copy.value = cloneBox(node.value);
            copy.minimum = cloneBox(node.minimum);
            copy.maximum = cloneBox(node.maximum);
            return copy;
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            NumberLerpExpression copy;
            copy.from = cloneBox(node.from);
            copy.to = cloneBox(node.to);
            copy.amount = cloneBox(node.amount);
            return copy;
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            NumberRandomRangeExpression copy;
            copy.minimum = cloneBox(node.minimum);
            copy.maximum = cloneBox(node.maximum);
            return copy;
        }
    }, value);
}

// One recursion for both the const and the mutable overload: `Expression` is
// either `NumberExpression` or `const NumberExpression`, and the cast on the
// way down restores the constness that unique_ptr::operator* drops.
template <typename Expression, typename Visit>
void visitVariables(Expression& expression, const Visit& visit) {
    const auto descend = [&](const NumberExpressionBox& box) {
        if (box) visitVariables(static_cast<Expression&>(*box), visit);
    };
    std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            visit(node);
        } else if constexpr (std::is_same_v<T, NumberLiteralExpression>
                             || std::is_same_v<T, NumberPropertyExpression>) {
            // leaves
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            descend(node.operand);
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            descend(node.left);
            descend(node.right);
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            descend(node.value);
            descend(node.minimum);
            descend(node.maximum);
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            descend(node.from);
            descend(node.to);
            descend(node.amount);
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            descend(node.minimum);
            descend(node.maximum);
        }
    }, expression.value());
}

bool sameBox(const NumberExpressionBox& left, const NumberExpressionBox& right) {
    if (!left && !right) return true;
    if (!left || !right) return false;
    return sameNumberExpression(*left, *right);
}

} // namespace

NumberExpression::NumberExpression()
    : value_(NumberLiteralExpression{}) {}

NumberExpression::NumberExpression(NumberExpressionVariant value)
    : value_(std::move(value)) {}

NumberExpression::NumberExpression(const NumberExpression& other)
    : value_(cloneVariant(other.value_)) {}

NumberExpression& NumberExpression::operator=(const NumberExpression& other) {
    if (this != &other) value_ = cloneVariant(other.value_);
    return *this;
}

NumberExpression NumberExpression::literal(double value) {
    return NumberExpression{NumberLiteralExpression{value}};
}

NumberExpressionKind NumberExpression::kind() const {
    return static_cast<NumberExpressionKind>(value_.index());
}

NumberExpressionBox boxNumberExpression(NumberExpression expression) {
    return std::make_unique<NumberExpression>(std::move(expression));
}

NumberExpression cloneNumberExpression(const NumberExpression& expression) {
    return NumberExpression{expression};
}

void forEachNumberVariableExpression(
    const NumberExpression& expression,
    const std::function<void(const NumberVariableExpression&)>& visit) {
    visitVariables(expression, visit);
}

void forEachNumberVariableExpression(
    NumberExpression& expression,
    const std::function<void(NumberVariableExpression&)>& visit) {
    visitVariables(expression, visit);
}

bool sameNumberExpression(const NumberExpression& left, const NumberExpression& right) {
    if (left.kind() != right.kind()) return false;
    return std::visit([&](const auto& lhs) -> bool {
        using T = std::decay_t<decltype(lhs)>;
        const T* rhs = std::get_if<T>(&right.value());
        if (!rhs) return false;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            return lhs.value == rhs->value;
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            return lhs.property == rhs->property;
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            return lhs.scope == rhs->scope && lhs.variableId == rhs->variableId;
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            return lhs.operation == rhs->operation && sameBox(lhs.operand, rhs->operand);
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            return lhs.operation == rhs->operation
                && sameBox(lhs.left, rhs->left)
                && sameBox(lhs.right, rhs->right);
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            return sameBox(lhs.value, rhs->value)
                && sameBox(lhs.minimum, rhs->minimum)
                && sameBox(lhs.maximum, rhs->maximum);
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            return sameBox(lhs.from, rhs->from)
                && sameBox(lhs.to, rhs->to)
                && sameBox(lhs.amount, rhs->amount);
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            return sameBox(lhs.minimum, rhs->minimum)
                && sameBox(lhs.maximum, rhs->maximum);
        }
    }, left.value());
}

bool isLiteralNumberExpression(const NumberExpression& expression) {
    return std::holds_alternative<NumberLiteralExpression>(expression.value());
}

std::optional<double> literalNumberValue(const NumberExpression& expression) {
    if (const auto* lit = std::get_if<NumberLiteralExpression>(&expression.value()))
        return lit->value;
    return std::nullopt;
}

bool isFiniteNumberLiteral(double value) {
    return std::isfinite(value);
}

LogicVec2Value LogicVec2Value::literal(double x, double y) {
    return LogicVec2Value{NumberExpression::literal(x), NumberExpression::literal(y)};
}

bool isLiteralLogicVec2Value(const LogicVec2Value& value) {
    return isLiteralNumberExpression(value.x) && isLiteralNumberExpression(value.y);
}

std::optional<Vec2> literalVec2Value(const LogicVec2Value& value) {
    const auto x = literalNumberValue(value.x);
    const auto y = literalNumberValue(value.y);
    if (!x || !y || !isFiniteNumberLiteral(*x) || !isFiniteNumberLiteral(*y))
        return std::nullopt;
    return Vec2{static_cast<float>(*x), static_cast<float>(*y)};
}

} // namespace ArtCade
