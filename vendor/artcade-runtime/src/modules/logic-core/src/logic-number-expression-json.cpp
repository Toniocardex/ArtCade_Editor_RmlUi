#include "logic-number-expression-json.h"

#include <cmath>
#include <optional>
#include <type_traits>

namespace ArtCade::Logic {
namespace {

const char* propertyToken(NumberProperty property) {
    switch (property) {
    case NumberProperty::SelfPositionX: return "self.position.x";
    case NumberProperty::SelfPositionY: return "self.position.y";
    case NumberProperty::SceneWorldWidth: return "scene.worldWidth";
    case NumberProperty::SceneWorldHeight: return "scene.worldHeight";
    case NumberProperty::DeltaSeconds: return "time.deltaSeconds";
    }
    return "";
}

std::optional<NumberProperty> propertyFromToken(const std::string& token) {
    if (token == "self.position.x") return NumberProperty::SelfPositionX;
    if (token == "self.position.y") return NumberProperty::SelfPositionY;
    if (token == "scene.worldWidth") return NumberProperty::SceneWorldWidth;
    if (token == "scene.worldHeight") return NumberProperty::SceneWorldHeight;
    if (token == "time.deltaSeconds") return NumberProperty::DeltaSeconds;
    return std::nullopt;
}

const char* unaryToken(NumberUnaryOperator op) {
    switch (op) {
    case NumberUnaryOperator::Negate: return "negate";
    case NumberUnaryOperator::Absolute: return "absolute";
    case NumberUnaryOperator::Floor: return "floor";
    case NumberUnaryOperator::Ceil: return "ceil";
    case NumberUnaryOperator::Round: return "round";
    }
    return "";
}

std::optional<NumberUnaryOperator> unaryFromToken(const std::string& token) {
    if (token == "negate") return NumberUnaryOperator::Negate;
    if (token == "absolute") return NumberUnaryOperator::Absolute;
    if (token == "floor") return NumberUnaryOperator::Floor;
    if (token == "ceil") return NumberUnaryOperator::Ceil;
    if (token == "round") return NumberUnaryOperator::Round;
    return std::nullopt;
}

const char* binaryToken(NumberBinaryOperator op) {
    switch (op) {
    case NumberBinaryOperator::Add: return "add";
    case NumberBinaryOperator::Subtract: return "subtract";
    case NumberBinaryOperator::Multiply: return "multiply";
    case NumberBinaryOperator::Divide: return "divide";
    case NumberBinaryOperator::Minimum: return "minimum";
    case NumberBinaryOperator::Maximum: return "maximum";
    }
    return "";
}

std::optional<NumberBinaryOperator> binaryFromToken(const std::string& token) {
    if (token == "add") return NumberBinaryOperator::Add;
    if (token == "subtract") return NumberBinaryOperator::Subtract;
    if (token == "multiply") return NumberBinaryOperator::Multiply;
    if (token == "divide") return NumberBinaryOperator::Divide;
    if (token == "minimum") return NumberBinaryOperator::Minimum;
    if (token == "maximum") return NumberBinaryOperator::Maximum;
    return std::nullopt;
}

bool readChild(const nlohmann::json& parent, const char* key, NumberExpression& out,
               std::string& error, bool allowStructured) {
    if (!parent.contains(key)) {
        error = std::string("Number expression missing child: ") + key;
        return false;
    }
    return numberExpressionFromJson(parent[key], out, error, allowStructured);
}

} // namespace

nlohmann::json numberExpressionToJson(const NumberExpression& expression) {
    return std::visit([](const auto& node) -> nlohmann::json {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            return node.value;
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            return {{"kind", "property"}, {"property", propertyToken(node.property)}};
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            return {{"kind", "variable"},
                    {"scope", node.scope == NumberVariableScope::Global ? "global" : "local"},
                    {"variableId", node.variableId}};
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            return {{"kind", "unary"},
                    {"operator", unaryToken(node.operation)},
                    {"operand", numberExpressionToJson(*node.operand)}};
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            return {{"kind", "binary"},
                    {"operator", binaryToken(node.operation)},
                    {"left", numberExpressionToJson(*node.left)},
                    {"right", numberExpressionToJson(*node.right)}};
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            return {{"kind", "clamp"},
                    {"value", numberExpressionToJson(*node.value)},
                    {"minimum", numberExpressionToJson(*node.minimum)},
                    {"maximum", numberExpressionToJson(*node.maximum)}};
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            return {{"kind", "lerp"},
                    {"from", numberExpressionToJson(*node.from)},
                    {"to", numberExpressionToJson(*node.to)},
                    {"amount", numberExpressionToJson(*node.amount)}};
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            return {{"kind", "randomRange"},
                    {"minimum", numberExpressionToJson(*node.minimum)},
                    {"maximum", numberExpressionToJson(*node.maximum)}};
        }
    }, expression.value());
}

bool numberExpressionFromJson(const nlohmann::json& json, NumberExpression& out,
                              std::string& error, bool allowStructured) {
    if (json.is_number()) {
        const double value = json.get<double>();
        if (!std::isfinite(value)) {
            error = "Number expression literal must be finite";
            return false;
        }
        out = NumberExpression::literal(value);
        return true;
    }
    if (!allowStructured) {
        error = "Structured number expressions require Logic Board schema 4";
        return false;
    }
    if (!json.is_object() || !json.contains("kind") || !json["kind"].is_string()) {
        error = "Number expression must be a number or object with kind";
        return false;
    }
    const std::string kind = json["kind"].get<std::string>();
    if (kind == "property") {
        if (!json.contains("property") || !json["property"].is_string()) {
            error = "property expression requires property";
            return false;
        }
        const auto property = propertyFromToken(json["property"].get<std::string>());
        if (!property) {
            error = "Unknown number property";
            return false;
        }
        out = NumberExpression{NumberPropertyExpression{*property}};
        return true;
    }
    if (kind == "variable") {
        NumberVariableExpression node;
        if (!json.contains("variableId") || !json["variableId"].is_string()) {
            error = "variable expression requires variableId";
            return false;
        }
        node.variableId = json["variableId"].get<std::string>();
        std::string scope = "local";
        if (json.contains("scope") && json["scope"].is_string())
            scope = json["scope"].get<std::string>();
        if (scope == "global") node.scope = NumberVariableScope::Global;
        else if (scope == "local") node.scope = NumberVariableScope::Local;
        else {
            error = "Unknown variable scope";
            return false;
        }
        out = NumberExpression{std::move(node)};
        return true;
    }
    if (kind == "unary") {
        if (!json.contains("operator") || !json["operator"].is_string()) {
            error = "unary expression requires operator";
            return false;
        }
        const auto op = unaryFromToken(json["operator"].get<std::string>());
        if (!op) {
            error = "Unknown unary operator";
            return false;
        }
        NumberExpression operand;
        if (!readChild(json, "operand", operand, error, allowStructured)) return false;
        NumberUnaryExpression node;
        node.operation = *op;
        node.operand = boxNumberExpression(std::move(operand));
        out = NumberExpression{std::move(node)};
        return true;
    }
    if (kind == "binary") {
        if (!json.contains("operator") || !json["operator"].is_string()) {
            error = "binary expression requires operator";
            return false;
        }
        const auto op = binaryFromToken(json["operator"].get<std::string>());
        if (!op) {
            error = "Unknown binary operator";
            return false;
        }
        NumberExpression left;
        NumberExpression right;
        if (!readChild(json, "left", left, error, allowStructured)) return false;
        if (!readChild(json, "right", right, error, allowStructured)) return false;
        NumberBinaryExpression node;
        node.operation = *op;
        node.left = boxNumberExpression(std::move(left));
        node.right = boxNumberExpression(std::move(right));
        out = NumberExpression{std::move(node)};
        return true;
    }
    if (kind == "clamp") {
        NumberExpression value;
        NumberExpression minimum;
        NumberExpression maximum;
        if (!readChild(json, "value", value, error, allowStructured)) return false;
        if (!readChild(json, "minimum", minimum, error, allowStructured)) return false;
        if (!readChild(json, "maximum", maximum, error, allowStructured)) return false;
        NumberClampExpression node;
        node.value = boxNumberExpression(std::move(value));
        node.minimum = boxNumberExpression(std::move(minimum));
        node.maximum = boxNumberExpression(std::move(maximum));
        out = NumberExpression{std::move(node)};
        return true;
    }
    if (kind == "lerp") {
        NumberExpression from;
        NumberExpression to;
        NumberExpression amount;
        if (!readChild(json, "from", from, error, allowStructured)) return false;
        if (!readChild(json, "to", to, error, allowStructured)) return false;
        if (!readChild(json, "amount", amount, error, allowStructured)) return false;
        NumberLerpExpression node;
        node.from = boxNumberExpression(std::move(from));
        node.to = boxNumberExpression(std::move(to));
        node.amount = boxNumberExpression(std::move(amount));
        out = NumberExpression{std::move(node)};
        return true;
    }
    if (kind == "randomRange") {
        NumberExpression minimum;
        NumberExpression maximum;
        if (!readChild(json, "minimum", minimum, error, allowStructured)) return false;
        if (!readChild(json, "maximum", maximum, error, allowStructured)) return false;
        NumberRandomRangeExpression node;
        node.minimum = boxNumberExpression(std::move(minimum));
        node.maximum = boxNumberExpression(std::move(maximum));
        out = NumberExpression{std::move(node)};
        return true;
    }
    error = "Unknown number expression kind";
    return false;
}

} // namespace ArtCade::Logic
