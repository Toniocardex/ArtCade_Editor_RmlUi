#pragma once

#include "../../../core/logic-number-expression.h"

#include <string>

namespace ArtCade::Logic {

/**
 * Shortest decimal form that reads back as the same double:
 * `strtod(numberLiteralText(x), nullptr) == x` for every finite `x`.
 *
 * Owned here and used by both the Code-style formatter and every Lua codegen
 * site (ADR-0038 Finding 1) so a literal reads the same value the field shows
 * and the value the game executes. @p value must be finite — validation
 * rejects non-finite literals before they can reach here; passing one is a
 * caller bug, not a data condition to recover from.
 */
std::string numberLiteralText(double value);

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
