#include "logic-number-expression-format.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace ArtCade::Logic {
namespace {

/**
 * Shortest decimal form that reads back as the same double. Starting at 6
 * significant digits keeps the everyday values an author typed (`0`, `100`,
 * `1.5`) looking like what they typed, and only widens when a value genuinely
 * needs it — a fixed high precision would turn `0.1` into `0.10000000000000001`
 * in the field.
 */
std::string codeLiteral(double value) {
    for (const int precision : {6, 15, 16, 17}) {
        std::ostringstream out;
        out << std::setprecision(precision) << value;
        const std::string text = out.str();
        if (std::strtod(text.c_str(), nullptr) == value) return text;
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

bool isCodeIdentifier(const std::string& name) {
    if (name.empty()) return false;
    const auto ok = [](unsigned char c, bool first) {
        return std::isalpha(c) != 0 || c == '_' || (!first && std::isdigit(c) != 0);
    };
    if (!ok(static_cast<unsigned char>(name[0]), true)) return false;
    for (std::size_t i = 1; i < name.size(); ++i)
        if (!ok(static_cast<unsigned char>(name[i]), false)) return false;
    return true;
}

/** `$score`, or `$'has spaces'` when the name is not a bare identifier. */
std::string codeVariable(const NumberVariableExpression& node) {
    std::string out = "$";
    if (node.scope == NumberVariableScope::Global) out += "global.";
    if (isCodeIdentifier(node.variableId)) return out + node.variableId;
    out += "'";
    for (const char c : node.variableId) {
        if (c == '\'' || c == '\\') out += '\\';
        out += c;
    }
    return out + "'";
}

const char* codePropertyName(NumberProperty property) {
    switch (property) {
    case NumberProperty::SelfPositionX: return "self.x";
    case NumberProperty::SelfPositionY: return "self.y";
    case NumberProperty::SceneWorldWidth: return "scene.width";
    case NumberProperty::SceneWorldHeight: return "scene.height";
    case NumberProperty::DeltaSeconds: return "delta";
    }
    return "?";
}

std::string formatCode(const NumberExpression& expression);

std::string formatCodeChild(const NumberExpressionBox& box) {
    // A null child cannot round-trip. Emitting `?` makes the text fail to
    // parse, which is honest; silently substituting 0 would rewrite the tree.
    if (!box) return "?";
    return formatCode(*box);
}

std::string formatCode(const NumberExpression& expression) {
    return std::visit([&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        std::ostringstream out;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            out << codeLiteral(node.value);
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            out << codePropertyName(node.property);
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            out << codeVariable(node);
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            switch (node.operation) {
            // Always parenthesised: `-5` parses as the literal -5, so an
            // unparenthesised `-` would round-trip Negate(5) into Literal(-5).
            case NumberUnaryOperator::Negate:
                out << "-(" << formatCodeChild(node.operand) << ")";
                break;
            case NumberUnaryOperator::Absolute:
                out << "abs(" << formatCodeChild(node.operand) << ")"; break;
            case NumberUnaryOperator::Floor:
                out << "floor(" << formatCodeChild(node.operand) << ")"; break;
            case NumberUnaryOperator::Ceil:
                out << "ceil(" << formatCodeChild(node.operand) << ")"; break;
            case NumberUnaryOperator::Round:
                out << "round(" << formatCodeChild(node.operand) << ")"; break;
            }
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            const char* op = nullptr;
            switch (node.operation) {
            case NumberBinaryOperator::Add: op = "+"; break;
            case NumberBinaryOperator::Subtract: op = "-"; break;
            case NumberBinaryOperator::Multiply: op = "*"; break;
            case NumberBinaryOperator::Divide: op = "/"; break;
            case NumberBinaryOperator::Minimum:
                out << "min(" << formatCodeChild(node.left) << ", "
                    << formatCodeChild(node.right) << ")";
                return out.str();
            case NumberBinaryOperator::Maximum:
                out << "max(" << formatCodeChild(node.left) << ", "
                    << formatCodeChild(node.right) << ")";
                return out.str();
            }
            // Fully parenthesised, so the tree shape survives without the
            // parser and formatter having to agree on precedence.
            out << "(" << formatCodeChild(node.left) << " " << op << " "
                << formatCodeChild(node.right) << ")";
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            out << "clamp(" << formatCodeChild(node.value) << ", "
                << formatCodeChild(node.minimum) << ", "
                << formatCodeChild(node.maximum) << ")";
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            out << "lerp(" << formatCodeChild(node.from) << ", "
                << formatCodeChild(node.to) << ", "
                << formatCodeChild(node.amount) << ")";
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            out << "random(" << formatCodeChild(node.minimum) << ", "
                << formatCodeChild(node.maximum) << ")";
        }
        return out.str();
    }, expression.value());
}

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
    if (style == NumberExpressionFormatStyle::Code) return formatCode(expression);
    return formatNode(expression, style);
}

} // namespace ArtCade::Logic
