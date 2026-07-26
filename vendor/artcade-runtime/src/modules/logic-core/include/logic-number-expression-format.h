#pragma once

#include "../../../core/logic-number-expression.h"

#include <string>

namespace ArtCade::Logic {

enum class NumberExpressionFormatStyle {
    Compact,
    Full,
    /**
     * The round-trip form (ADR-0029): `random(0, 100)`, `self.x`, `$score`.
     *
     * Unlike Compact and Full, which are prose for display, this is the syntax
     * the author types and `parseNumberExpression` is its exact inverse —
     * `parse(format(e, Code)) == e` for every expression. A change here that is
     * not matched in the parser corrupts what the author wrote, which is why
     * the round-trip is a test and not a convention.
     */
    Code,
};

std::string formatNumberExpression(const NumberExpression& expression,
                                   NumberExpressionFormatStyle style);

} // namespace ArtCade::Logic
