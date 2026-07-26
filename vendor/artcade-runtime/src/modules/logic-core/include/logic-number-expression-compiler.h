#pragma once

#include "../../../core/logic-number-expression.h"

#include <string>

namespace ArtCade::Logic {

struct CompiledNumberExpression {
    bool ok = false;
    std::string luaSource;
    std::string error;
};

/** Emit Lua that evaluates to a number (deterministic child order). */
CompiledNumberExpression compileNumberExpressionToLua(
    const NumberExpression& expression);

} // namespace ArtCade::Logic
