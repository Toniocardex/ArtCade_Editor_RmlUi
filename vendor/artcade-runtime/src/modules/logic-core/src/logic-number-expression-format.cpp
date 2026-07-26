#include "logic-number-expression-format.h"

#include <sstream>

namespace ArtCade::Logic {
namespace {

const char* propertyLabel(NumberProperty property, NumberExpressionFormatStyle style) {
    const bool compact = style == NumberExpressionFormatStyle::Compact;
    switch (property) {
    case NumberProperty::SelfPositionX: return compact ? "Self X" : "Self Position X";
    case NumberProperty::SelfPositionY: return compact ? "Self Y" : "Self Position Y";
    case NumberProperty::SceneWorldWidth: return compact ? "Scene Width" : "Scene World Width";
    case NumberProperty::SceneWorldHeight: return compact ? "Scene Height" : "Scene World Height";
    case NumberProperty::DeltaSeconds: return "Delta Seconds";
    }
    return "?";
}

std::string formatNode(const NumberExpression& expression,
                       NumberExpressionFormatStyle style);

std::string formatChild(const NumberExpressionBox& box,
                        NumberExpressionFormatStyle style) {
    if (!box) return "?";
    return formatNode(*box, style);
}

std::string formatNode(const NumberExpression& expression,
                       NumberExpressionFormatStyle style) {
    return std::visit([&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        std::ostringstream out;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            out << node.value;
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            out << propertyLabel(node.property, style);
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            out << (node.scope == NumberVariableScope::Global ? "Global." : "Local.")
                << node.variableId;
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            const char* name = "Negate";
            switch (node.operation) {
            case NumberUnaryOperator::Negate: name = "Negate"; break;
            case NumberUnaryOperator::Absolute: name = "Abs"; break;
            case NumberUnaryOperator::Floor: name = "Floor"; break;
            case NumberUnaryOperator::Ceil: name = "Ceil"; break;
            case NumberUnaryOperator::Round: name = "Round"; break;
            }
            out << name << "(" << formatChild(node.operand, style) << ")";
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            const char* op = "+";
            switch (node.operation) {
            case NumberBinaryOperator::Add: op = "+"; break;
            case NumberBinaryOperator::Subtract: op = "-"; break;
            case NumberBinaryOperator::Multiply: op = "*"; break;
            case NumberBinaryOperator::Divide: op = "/"; break;
            case NumberBinaryOperator::Minimum:
                out << "Min(" << formatChild(node.left, style) << ", "
                    << formatChild(node.right, style) << ")";
                return out.str();
            case NumberBinaryOperator::Maximum:
                out << "Max(" << formatChild(node.left, style) << ", "
                    << formatChild(node.right, style) << ")";
                return out.str();
            }
            out << "(" << formatChild(node.left, style) << " " << op << " "
                << formatChild(node.right, style) << ")";
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            out << "Clamp(" << formatChild(node.value, style) << ", "
                << formatChild(node.minimum, style) << ", "
                << formatChild(node.maximum, style) << ")";
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            out << "Lerp(" << formatChild(node.from, style) << ", "
                << formatChild(node.to, style) << ", "
                << formatChild(node.amount, style) << ")";
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            const char* name =
                style == NumberExpressionFormatStyle::Compact ? "Random" : "Random Range";
            out << name << "(" << formatChild(node.minimum, style) << ", "
                << formatChild(node.maximum, style) << ")";
        }
        return out.str();
    }, expression.value());
}

} // namespace

std::string formatNumberExpression(const NumberExpression& expression,
                                   NumberExpressionFormatStyle style) {
    return formatNode(expression, style);
}

} // namespace ArtCade::Logic
