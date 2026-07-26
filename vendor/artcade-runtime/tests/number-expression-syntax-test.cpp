// ADR-0029 — the Code syntax and its parser.
//
// The load-bearing claim is the round trip: `parse(format(e, Code)) == e` for
// every expression. Under ADR-0028 the formatter was a display convenience; now
// it is half of what the author types, so a formatting change that is not
// matched in the parser silently rewrites their expression. That is what the
// first half of this file exists to prevent.

#include "logic-number-expression-format.h"
#include "logic-number-expression-parse.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::Logic;

namespace {

int passed = 0;
int failed = 0;

#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

std::string code(const NumberExpression& expression) {
    return formatNumberExpression(expression, NumberExpressionFormatStyle::Code);
}

NumberExpression binary(NumberBinaryOperator op, NumberExpression left,
                        NumberExpression right) {
    NumberBinaryExpression node;
    node.operation = op;
    node.left = boxNumberExpression(std::move(left));
    node.right = boxNumberExpression(std::move(right));
    return NumberExpression{std::move(node)};
}

NumberExpression unary(NumberUnaryOperator op, NumberExpression operand) {
    NumberUnaryExpression node;
    node.operation = op;
    node.operand = boxNumberExpression(std::move(operand));
    return NumberExpression{std::move(node)};
}

NumberExpression property(NumberProperty which) {
    return NumberExpression{NumberPropertyExpression{which}};
}

NumberExpression variable(NumberVariableScope scope, std::string id) {
    NumberVariableExpression node;
    node.scope = scope;
    node.variableId = std::move(id);
    return NumberExpression{std::move(node)};
}

/** Every node kind, plus the shapes that are easy to get wrong. */
std::vector<NumberExpression> catalogue() {
    std::vector<NumberExpression> all;
    all.push_back(NumberExpression::literal(0.0));
    all.push_back(NumberExpression::literal(100.0));
    all.push_back(NumberExpression::literal(1.5));
    all.push_back(NumberExpression::literal(-5.0));
    // Values a naive 6-digit formatter would round away.
    all.push_back(NumberExpression::literal(0.1));
    all.push_back(NumberExpression::literal(1.0 / 3.0));
    all.push_back(NumberExpression::literal(123456789.123456789));
    all.push_back(NumberExpression::literal(1e-9));

    all.push_back(property(NumberProperty::SelfPositionX));
    all.push_back(property(NumberProperty::SelfPositionY));
    all.push_back(property(NumberProperty::SceneWorldWidth));
    all.push_back(property(NumberProperty::SceneWorldHeight));
    all.push_back(property(NumberProperty::DeltaSeconds));

    all.push_back(variable(NumberVariableScope::Local, "score"));
    all.push_back(variable(NumberVariableScope::Global, "score"));
    // A name that is not a bare identifier still has to survive.
    all.push_back(variable(NumberVariableScope::Local, "has spaces"));
    all.push_back(variable(NumberVariableScope::Global, "it's odd"));
    // A local variable literally called `global` must not read as a scope.
    all.push_back(variable(NumberVariableScope::Local, "global"));

    for (const NumberUnaryOperator op :
         {NumberUnaryOperator::Negate, NumberUnaryOperator::Absolute,
          NumberUnaryOperator::Floor, NumberUnaryOperator::Ceil,
          NumberUnaryOperator::Round}) {
        all.push_back(unary(op, NumberExpression::literal(5.0)));
        // Negate over a *negative* literal is the pair the sign fold can break.
        all.push_back(unary(op, NumberExpression::literal(-5.0)));
    }

    for (const NumberBinaryOperator op :
         {NumberBinaryOperator::Add, NumberBinaryOperator::Subtract,
          NumberBinaryOperator::Multiply, NumberBinaryOperator::Divide,
          NumberBinaryOperator::Minimum, NumberBinaryOperator::Maximum}) {
        all.push_back(binary(op, NumberExpression::literal(2.0),
                             NumberExpression::literal(3.0)));
    }

    NumberClampExpression clamp;
    clamp.value = boxNumberExpression(property(NumberProperty::SelfPositionX));
    clamp.minimum = boxNumberExpression(NumberExpression::literal(0.0));
    clamp.maximum = boxNumberExpression(property(NumberProperty::SceneWorldWidth));
    all.push_back(NumberExpression{std::move(clamp)});

    NumberLerpExpression lerp;
    lerp.from = boxNumberExpression(NumberExpression::literal(0.0));
    lerp.to = boxNumberExpression(NumberExpression::literal(100.0));
    lerp.amount = boxNumberExpression(property(NumberProperty::DeltaSeconds));
    all.push_back(NumberExpression{std::move(lerp)});

    NumberRandomRangeExpression random;
    random.minimum = boxNumberExpression(NumberExpression::literal(0.0));
    random.maximum = boxNumberExpression(NumberExpression::literal(100.0));
    all.push_back(NumberExpression{std::move(random)});

    // Nesting: associativity and mixed precedence are where a formatter that
    // drops parentheses stops being an inverse.
    all.push_back(binary(NumberBinaryOperator::Subtract,
                         binary(NumberBinaryOperator::Subtract,
                                NumberExpression::literal(10.0),
                                NumberExpression::literal(3.0)),
                         NumberExpression::literal(2.0)));
    all.push_back(binary(NumberBinaryOperator::Subtract,
                         NumberExpression::literal(10.0),
                         binary(NumberBinaryOperator::Subtract,
                                NumberExpression::literal(3.0),
                                NumberExpression::literal(2.0))));
    all.push_back(binary(NumberBinaryOperator::Multiply,
                         binary(NumberBinaryOperator::Add,
                                NumberExpression::literal(1.0),
                                NumberExpression::literal(2.0)),
                         NumberExpression::literal(3.0)));
    all.push_back(binary(NumberBinaryOperator::Add,
                         NumberExpression::literal(1.0),
                         binary(NumberBinaryOperator::Multiply,
                                NumberExpression::literal(2.0),
                                NumberExpression::literal(3.0))));
    return all;
}

void testRoundTrip() {
    for (const NumberExpression& original : catalogue()) {
        const std::string text = code(original);
        const NumberExpressionParseResult parsed = parseNumberExpression(text);
        if (!parsed.ok) {
            ++failed;
            std::cerr << "FAIL round trip: '" << text << "' did not parse — "
                      << parsed.error.message << "\n";
            continue;
        }
        if (!sameNumberExpression(parsed.value, original)) {
            ++failed;
            std::cerr << "FAIL round trip: '" << text << "' parsed to '"
                      << code(parsed.value) << "'\n";
            continue;
        }
        ++passed;
    }
}

/** The text an author actually types, and what it must mean. */
void testAuthoredText() {
    const auto parses = [](const std::string& text) {
        const NumberExpressionParseResult result = parseNumberExpression(text);
        return result.ok ? code(result.value) : std::string{"<error>"};
    };
    CHECK(parses("100") == "100");
    CHECK(parses("  100  ") == "100");
    CHECK(parses("-5") == "-5");
    CHECK(parses("random(0, 100)") == "random(0, 100)");
    CHECK(parses("self.x") == "self.x");
    CHECK(parses("$score") == "$score");
    CHECK(parses("$global.score") == "$global.score");
    CHECK(parses("self.x + 10") == "(self.x + 10)");
    CHECK(parses("clamp(self.x + 10, 0, scene.width)")
          == "clamp((self.x + 10), 0, scene.width)");
    // Precedence must be real, not left-to-right.
    CHECK(parses("1 + 2 * 3") == "(1 + (2 * 3))");
    CHECK(parses("(1 + 2) * 3") == "((1 + 2) * 3)");
    CHECK(parses("10 - 3 - 2") == "((10 - 3) - 2)");
    // Redundant parentheses are the author's, and collapse into the same tree.
    CHECK(parses("((((5))))") == "5");
    CHECK(parses("1e3") == "1000");
    CHECK(parses("-(5)") != "-5");  // a real Negate node, not a negative literal
}

void testRejects() {
    const auto error = [](const std::string& text) {
        return !parseNumberExpression(text).ok;
    };
    CHECK(error(""));
    CHECK(error("   "));
    CHECK(error("1 +"));
    CHECK(error("(1 + 2"));
    CHECK(error("1 + 2)"));
    CHECK(error("random(0)"));            // arity
    CHECK(error("random(0, 1, 2)"));      // arity
    CHECK(error("clamp(1, 2)"));          // arity
    CHECK(error("nope(1)"));              // unknown function
    CHECK(error("self.z"));               // unknown property
    CHECK(error("$"));                    // no variable name
    CHECK(error("$'unterminated"));
    CHECK(error("100 200"));              // trailing garbage
    CHECK(error("1 & 2"));
    // The interpreter must stay unreachable: none of this is a valid program.
    CHECK(error("os.time()"));
    CHECK(error("require('os')"));
    CHECK(error("1; print(1)"));

    // An error has to be able to point at the offending character.
    const NumberExpressionParseResult result = parseNumberExpression("1 + nope(2)");
    CHECK(!result.ok);
    CHECK(!result.error.message.empty());
    CHECK(result.error.offset == 4);
}

void testLimits() {
    // Deeper than the load limit: the parser refuses rather than building a
    // tree that validation would have to reject later.
    std::string deep;
    for (int i = 0; i < static_cast<int>(kMaximumNumberExpressionDepth) + 4; ++i)
        deep += "abs(";
    deep += "1";
    for (int i = 0; i < static_cast<int>(kMaximumNumberExpressionDepth) + 4; ++i)
        deep += ")";
    CHECK(!parseNumberExpression(deep).ok);

    std::string wide = "1";
    for (std::size_t i = 0; i < kMaximumNumberExpressionNodes + 8; ++i) wide += " + 1";
    CHECK(!parseNumberExpression(wide).ok);
}

} // namespace

int main() {
    testRoundTrip();
    testAuthoredText();
    testRejects();
    testLimits();
    std::cout << "number-expression-syntax-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
