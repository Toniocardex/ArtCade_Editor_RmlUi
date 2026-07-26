#pragma once

#include "../../../core/logic-number-expression.h"
#include "logic-core.h"

#include <string>

namespace ArtCade::Logic {

/**
 * ADR-0028 Expression Context — which properties/variables are valid for the
 * current event and Object Type. Catalogs list declared variables; validation
 * still requires Number type for expression references.
 */
struct NumberExpressionContext {
    bool selfAvailable = true;
    bool sceneAvailable = true;
    bool deltaSecondsAvailable = false;
    const std::vector<GameVariableDefinition>* localVariables = nullptr;
    const std::vector<GameVariableDefinition>* globalVariables = nullptr;
};

struct NumberExpressionValidationResult {
    bool ok = false;
    std::string errorCode;
    std::string message;
};

NumberExpressionValidationResult validateNumberExpression(
    const NumberExpression& expression,
    const NumberExpressionContext& context);

/** Require a finite literal root (LiteralOnly policy). */
NumberExpressionValidationResult requireLiteralNumberExpression(
    const NumberExpression& expression);

} // namespace ArtCade::Logic
