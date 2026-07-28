#include "logic-number-expression-format.h"

#include "logic-number-expression-parse.h"

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace ArtCade::Logic {

/**
 * Starting at 6 significant digits keeps the everyday values an author typed
 * (`0`, `100`, `1.5`) looking like what they typed, and only widens when a
 * value genuinely needs it — a fixed high precision would turn `0.1` into
 * `0.10000000000000001` in the field.
 */
std::string numberLiteralText(double value) {
    assert(std::isfinite(value)
           && "numberLiteralText requires a finite value; validation must "
              "reject non-finite literals before they reach codegen or the field");
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

namespace {

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

/**
 * Binding strength, matching the grammar's `expr := term (('+'|'-') term)*`
 * and `term := factor (('*'|'/') factor)*`.
 *
 * Everything else is self-delimiting — a literal, a name, or a call whose
 * arguments sit inside its own parentheses — including unary negate, which is
 * always written `-(x)`. Those never need wrapping, so they get the strongest
 * binding.
 */
constexpr int kCodeAtomPrecedence = 3;

int codePrecedence(const NumberExpression& expression) {
    if (const auto* binary = std::get_if<NumberBinaryExpression>(&expression.value())) {
        switch (binary->operation) {
        case NumberBinaryOperator::Add:
        case NumberBinaryOperator::Subtract: return 1;
        case NumberBinaryOperator::Multiply:
        case NumberBinaryOperator::Divide: return 2;
        // min/max are calls: `min(a, b)`, not an infix operator.
        case NumberBinaryOperator::Minimum:
        case NumberBinaryOperator::Maximum: break;
        }
    }
    return kCodeAtomPrecedence;
}

/**
 * @p minimumPrecedence is what the surrounding context requires: anything that
 * binds more loosely has to be wrapped or the text would read back as a
 * different tree. Parentheses the author did not need are noise — the field
 * shows this text, so `self.x + 10` must come back as `self.x + 10`.
 */
std::string formatCode(const NumberExpression& expression, int minimumPrecedence);

std::string formatCodeChild(const NumberExpressionBox& box, int minimumPrecedence) {
    // A null child cannot round-trip. Emitting `?` makes the text fail to
    // parse, which is honest; silently substituting 0 would rewrite the tree.
    if (!box) return "?";
    return formatCode(*box, minimumPrecedence);
}

/** Argument position: a call's own parentheses already delimit it. */
std::string formatCodeChild(const NumberExpressionBox& box) {
    return formatCodeChild(box, 0);
}

std::string formatCode(const NumberExpression& expression, int minimumPrecedence) {
    return std::visit([&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        std::ostringstream out;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            out << numberLiteralText(node.value);
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            out << codePropertyName(node.property);
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            out << numberExpressionVariableToken(node.scope, node.variableId);
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
            // Both operators are left-associative, so an equal-precedence
            // child is safe on the left (`a - b - c` is `(a - b) - c` already)
            // but not on the right: `a - (b - c)` keeps its parentheses or it
            // reads back as a different tree.
            const int precedence = codePrecedence(expression);
            std::string text = formatCodeChild(node.left, precedence) + " " + op + " "
                             + formatCodeChild(node.right, precedence + 1);
            if (precedence < minimumPrecedence) text = "(" + text + ")";
            out << text;
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
    // Top level: nothing surrounds the expression, so nothing needs wrapping.
    if (style == NumberExpressionFormatStyle::Code) return formatCode(expression, 0);
    return formatNode(expression, style);
}

} // namespace ArtCade::Logic
