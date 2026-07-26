#pragma once

#include "../../../core/logic-number-expression.h"

#include <string>

namespace ArtCade::Logic {

enum class NumberExpressionFormatStyle {
    Compact,
    Full,
};

std::string formatNumberExpression(const NumberExpression& expression,
                                   NumberExpressionFormatStyle style);

} // namespace ArtCade::Logic
