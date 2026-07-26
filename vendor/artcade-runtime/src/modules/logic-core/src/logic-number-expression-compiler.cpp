#include "logic-number-expression-compiler.h"

#include <sstream>

namespace ArtCade::Logic {
namespace {

bool emit(const NumberExpression& expression, std::ostringstream& lua, std::string& error);

bool emitChild(const NumberExpressionBox& box, std::ostringstream& lua, std::string& error) {
    if (!box) {
        error = "Incomplete number expression";
        return false;
    }
    return emit(*box, lua, error);
}

bool emit(const NumberExpression& expression, std::ostringstream& lua, std::string& error) {
    return std::visit([&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteralExpression>) {
            lua << node.value;
            return true;
        } else if constexpr (std::is_same_v<T, NumberPropertyExpression>) {
            switch (node.property) {
            case NumberProperty::SelfPositionX:
                lua << "context.self:get_position_x()";
                break;
            case NumberProperty::SelfPositionY:
                lua << "context.self:get_position_y()";
                break;
            case NumberProperty::SceneWorldWidth:
                lua << "context:scene_world_width()";
                break;
            case NumberProperty::SceneWorldHeight:
                lua << "context:scene_world_height()";
                break;
            case NumberProperty::DeltaSeconds:
                lua << "context.delta_seconds";
                break;
            }
            return true;
        } else if constexpr (std::is_same_v<T, NumberVariableExpression>) {
            if (node.scope == NumberVariableScope::Local) {
                lua << "context:get_local_number('" << node.variableId << "')";
            } else {
                lua << "context:get_global_number('" << node.variableId << "')";
            }
            return true;
        } else if constexpr (std::is_same_v<T, NumberUnaryExpression>) {
            switch (node.operation) {
            case NumberUnaryOperator::Negate: lua << "-("; break;
            case NumberUnaryOperator::Absolute: lua << "math.abs("; break;
            case NumberUnaryOperator::Floor: lua << "math.floor("; break;
            case NumberUnaryOperator::Ceil: lua << "math.ceil("; break;
            case NumberUnaryOperator::Round:
                lua << "math.floor((";
                break;
            }
            if (!emitChild(node.operand, lua, error)) return false;
            if (node.operation == NumberUnaryOperator::Round) lua << ")+0.5)";
            else lua << ")";
            return true;
        } else if constexpr (std::is_same_v<T, NumberBinaryExpression>) {
            if (node.operation == NumberBinaryOperator::Divide) {
                lua << "logic.number.divide(";
                if (!emitChild(node.left, lua, error)) return false;
                lua << ", ";
                if (!emitChild(node.right, lua, error)) return false;
                lua << ")";
                return true;
            }
            if (node.operation == NumberBinaryOperator::Minimum
                || node.operation == NumberBinaryOperator::Maximum) {
                lua << (node.operation == NumberBinaryOperator::Minimum ? "math.min("
                                                                        : "math.max(");
                if (!emitChild(node.left, lua, error)) return false;
                lua << ", ";
                if (!emitChild(node.right, lua, error)) return false;
                lua << ")";
                return true;
            }
            lua << "(";
            if (!emitChild(node.left, lua, error)) return false;
            switch (node.operation) {
            case NumberBinaryOperator::Add: lua << " + "; break;
            case NumberBinaryOperator::Subtract: lua << " - "; break;
            case NumberBinaryOperator::Multiply: lua << " * "; break;
            default: break;
            }
            if (!emitChild(node.right, lua, error)) return false;
            lua << ")";
            return true;
        } else if constexpr (std::is_same_v<T, NumberClampExpression>) {
            lua << "logic.number.clamp(";
            if (!emitChild(node.value, lua, error)) return false;
            lua << ", ";
            if (!emitChild(node.minimum, lua, error)) return false;
            lua << ", ";
            if (!emitChild(node.maximum, lua, error)) return false;
            lua << ")";
            return true;
        } else if constexpr (std::is_same_v<T, NumberLerpExpression>) {
            lua << "logic.number.lerp(";
            if (!emitChild(node.from, lua, error)) return false;
            lua << ", ";
            if (!emitChild(node.to, lua, error)) return false;
            lua << ", ";
            if (!emitChild(node.amount, lua, error)) return false;
            lua << ")";
            return true;
        } else {
            static_assert(std::is_same_v<T, NumberRandomRangeExpression>);
            lua << "logic.random.range(context, ";
            if (!emitChild(node.minimum, lua, error)) return false;
            lua << ", ";
            if (!emitChild(node.maximum, lua, error)) return false;
            lua << ")";
            return true;
        }
    }, expression.value());
}

} // namespace

CompiledNumberExpression compileNumberExpressionToLua(
    const NumberExpression& expression) {
    CompiledNumberExpression result;
    std::ostringstream lua;
    std::string error;
    if (!emit(expression, lua, error)) {
        result.error = std::move(error);
        return result;
    }
    result.ok = true;
    result.luaSource = lua.str();
    return result;
}

} // namespace ArtCade::Logic
