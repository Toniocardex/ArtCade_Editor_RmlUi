#pragma once

#include "../../../core/logic-number-expression.h"

#include <cstddef>
#include <string>

namespace ArtCade::Logic {

/**
 * Where the text stopped making sense. @p offset is a byte index into the
 * input, so the editor can point at the character rather than only saying the
 * expression is wrong.
 */
struct NumberExpressionParseError {
    std::string message;
    std::size_t offset = 0;
};

struct NumberExpressionParseResult {
    bool ok = false;
    NumberExpression value;
    NumberExpressionParseError error;
};

/**
 * Text → AST for the `Code` syntax (ADR-0029), the exact inverse of
 * `formatNumberExpression(…, Code)`.
 *
 * The grammar is closed: it yields the ADR-0028 node types and nothing else,
 * so authored text can never reach the interpreter. Depth and node-count limits
 * are the same ones load and authoring already enforce.
 */
NumberExpressionParseResult parseNumberExpression(const std::string& text);

} // namespace ArtCade::Logic
