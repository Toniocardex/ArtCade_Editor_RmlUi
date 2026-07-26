#pragma once

#include "../../../core/logic-number-expression.h"

#include <cstddef>
#include <string>
#include <vector>

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

/**
 * One entry the editor can offer while the author types. The vocabulary lives
 * here, next to the parser that defines it, so a function added to the grammar
 * cannot be forgotten in the completion list.
 */
struct NumberExpressionCompletion {
    /** Text inserted into the field, e.g. `clamp(` or `self.x`. */
    std::string insert;
    /** What the author sees, e.g. `clamp(value, min, max)`. */
    std::string label;
    /** One line of explanation. */
    std::string summary;
};

/** Context values and functions. Project variables are the editor's to add. */
const std::vector<NumberExpressionCompletion>& numberExpressionCompletions();

/**
 * The partial token at the end of @p text — what the author is midway through
 * typing, so `clamp(self.` yields `self.` and the list narrows to it.
 *
 * Which characters can belong to a name is grammar, so it lives here with the
 * parser rather than being re-derived by the editor: two copies of this rule
 * drift, and the one that drifts is the one that decides what a completion
 * replaces.
 */
std::string numberExpressionTokenPrefix(const std::string& text);

/** Replace that trailing token with @p insert, so `cl` + `clamp(` is not
 *  `clclamp(`. The inverse half of numberExpressionTokenPrefix. */
std::string applyNumberExpressionCompletion(const std::string& text,
                                            const std::string& insert);

/**
 * How a variable is spelled in Code syntax: `$score`, `$global.score`, and
 * `$'has spaces'` when the name is not a bare identifier.
 *
 * Callers that offer a variable to the author MUST build the token with this —
 * a hand-written `"$" + name` is right until someone names a variable with a
 * space, and then it inserts text that does not parse.
 */
std::string numberExpressionVariableToken(NumberVariableScope scope,
                                          const std::string& variableId);

} // namespace ArtCade::Logic
