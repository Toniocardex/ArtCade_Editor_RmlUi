#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace ArtCade {

/** Maximum nesting depth for NumberExpression trees (authoring + load). */
inline constexpr std::size_t kMaximumNumberExpressionDepth = 16;
/** Maximum total nodes in one NumberExpression tree. */
inline constexpr std::size_t kMaximumNumberExpressionNodes = 128;
/** Denominator magnitude at or below this is an invalid divide. */
inline constexpr double kExpressionDivisionEpsilon = 1.0e-8;

enum class NumberExpressionKind {
    Literal,
    Property,
    Variable,
    Unary,
    Binary,
    Clamp,
    Lerp,
    RandomRange,
};

enum class NumberProperty {
    SelfPositionX,
    SelfPositionY,
    SceneWorldWidth,
    SceneWorldHeight,
    DeltaSeconds,
};

enum class NumberVariableScope {
    Local,
    Global,
};

enum class NumberUnaryOperator {
    Negate,
    Absolute,
    Floor,
    Ceil,
    Round,
};

enum class NumberBinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Minimum,
    Maximum,
};

struct NumberExpression;

/** Exclusive ownership of a child expression; deep-copied with the parent. */
using NumberExpressionBox = std::unique_ptr<NumberExpression>;

struct NumberLiteralExpression {
    double value = 0.0;
};

struct NumberPropertyExpression {
    NumberProperty property = NumberProperty::SelfPositionX;
};

struct NumberVariableExpression {
    NumberVariableScope scope = NumberVariableScope::Local;
    std::string variableId;
};

struct NumberUnaryExpression {
    NumberUnaryOperator operation = NumberUnaryOperator::Negate;
    NumberExpressionBox operand;
};

struct NumberBinaryExpression {
    NumberBinaryOperator operation = NumberBinaryOperator::Add;
    NumberExpressionBox left;
    NumberExpressionBox right;
};

struct NumberClampExpression {
    NumberExpressionBox value;
    NumberExpressionBox minimum;
    NumberExpressionBox maximum;
};

struct NumberLerpExpression {
    NumberExpressionBox from;
    NumberExpressionBox to;
    NumberExpressionBox amount;
};

struct NumberRandomRangeExpression {
    NumberExpressionBox minimum;
    NumberExpressionBox maximum;
};

using NumberExpressionVariant = std::variant<
    NumberLiteralExpression,
    NumberPropertyExpression,
    NumberVariableExpression,
    NumberUnaryExpression,
    NumberBinaryExpression,
    NumberClampExpression,
    NumberLerpExpression,
    NumberRandomRangeExpression>;

/**
 * Typed number expression AST (ADR-0028). Value semantics via deep copy.
 * Storage only — no JSON, Lua, or validation in this header.
 */
class NumberExpression {
public:
    NumberExpression();
    explicit NumberExpression(NumberExpressionVariant value);
    NumberExpression(const NumberExpression& other);
    NumberExpression& operator=(const NumberExpression& other);
    NumberExpression(NumberExpression&&) noexcept = default;
    NumberExpression& operator=(NumberExpression&&) noexcept = default;
    ~NumberExpression() = default;

    static NumberExpression literal(double value);

    NumberExpressionKind kind() const;
    const NumberExpressionVariant& value() const { return value_; }
    NumberExpressionVariant& value() { return value_; }

private:
    NumberExpressionVariant value_;
};

NumberExpressionBox boxNumberExpression(NumberExpression expression);
NumberExpression cloneNumberExpression(const NumberExpression& expression);
bool sameNumberExpression(const NumberExpression& left, const NumberExpression& right);
bool isLiteralNumberExpression(const NumberExpression& expression);
std::optional<double> literalNumberValue(const NumberExpression& expression);
bool isFiniteNumberLiteral(double value);

/** Vec2 Logic value whose components are NumberExpressions (ADR-0028). */
struct LogicVec2Value {
    NumberExpression x = NumberExpression::literal(0.0);
    NumberExpression y = NumberExpression::literal(0.0);

    static LogicVec2Value literal(double x, double y);
};

bool isLiteralLogicVec2Value(const LogicVec2Value& value);

} // namespace ArtCade
