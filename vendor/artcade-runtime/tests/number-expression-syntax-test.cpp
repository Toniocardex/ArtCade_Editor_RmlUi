// ADR-0029 — the Code syntax and its parser.
//
// The load-bearing claim is the round trip: `parse(format(e, Code)) == e` for
// every expression. Under ADR-0028 the formatter was a display convenience; now
// it is half of what the author types, so a formatting change that is not
// matched in the parser silently rewrites their expression. That is what the
// first half of this file exists to prevent.

#include "logic-number-expression-compiler.h"
#include "logic-number-expression-format.h"
#include "logic-number-expression-json.h"
#include "logic-number-expression-parse.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    // The field shows this text, so it carries only the parentheses the tree
    // actually needs — the ones an author would have written themselves.
    CHECK(parses("self.x + 10") == "self.x + 10");
    CHECK(parses("clamp(self.x + 10, 0, scene.width)")
          == "clamp(self.x + 10, 0, scene.width)");
    // Precedence must be real, not left-to-right.
    CHECK(parses("1 + 2 * 3") == "1 + 2 * 3");
    CHECK(parses("(1 + 2) * 3") == "(1 + 2) * 3");
    CHECK(parses("10 - 3 - 2") == "10 - 3 - 2");
    // Kept, because dropping them would change the tree: both operators are
    // left-associative, so an equal-precedence child on the right is not free.
    CHECK(parses("10 - (3 - 2)") == "10 - (3 - 2)");
    CHECK(parses("100 / (5 / 2)") == "100 / (5 / 2)");
    CHECK(parses("2 * (1 + 3)") == "2 * (1 + 3)");
    CHECK(parses("(1 + 2) / 3") == "(1 + 2) / 3");
    // Nesting keeps only the inner pair.
    CHECK(parses("((self.x + 10) * 2)") == "(self.x + 10) * 2");
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

/**
 * ADR-0038 Finding 1: a literal must read back to the same double on every
 * path a value travels — the field (formatter), the interpreter (Lua
 * compiler), and disk (JSON codec) — and the formatter and compiler must
 * agree on the exact text, since they now share one writer.
 */
void testLiteralFidelity() {
    const std::vector<double> corpus = {
        0.0, -0.0, 1.0, -5.0, 100.0,
        100000.5, 1234567.0, 1.0000001, 123456789.25, 3.14159265358979,
        0.1, 1.0 / 3.0,
        // The exact boundaries Finding 1 and Finding 4 measured.
        0.49999999999999994,
        4503599627370497.0,  // 2^52 + 1
        1e308, -1e308,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(),
    };
    for (const double value : corpus) {
        const NumberExpression expression = NumberExpression::literal(value);

        const std::string formatted = code(expression);
        CHECK(std::strtod(formatted.c_str(), nullptr) == value);

        const CompiledNumberExpression compiled = compileNumberExpressionToLua(expression);
        CHECK(compiled.ok);
        if (!compiled.ok) continue;
        CHECK(std::strtod(compiled.luaSource.c_str(), nullptr) == value);
        // The formatter and the compiler must emit identical text: one
        // shared writer, not two that can drift.
        CHECK(compiled.luaSource == formatted);

        const nlohmann::json json = numberExpressionToJson(expression);
        NumberExpression decoded;
        std::string error;
        CHECK(numberExpressionFromJson(json, decoded, error));
        const auto decodedValue = literalNumberValue(decoded);
        CHECK(decodedValue.has_value());
        if (decodedValue) CHECK(*decodedValue == value);
    }
}

/**
 * ADR-0038 Finding 2: the grammar-recursion cap must stop the descent before
 * the C++ stack does, for every shape that used to reach the parser
 * unbounded — and it must not newly reject anything that parsed before.
 */
void testParseRecursionBounded() {
    const int wellPast = static_cast<int>(kMaximumNumberExpressionParseDepth) * 4;

    std::string parens;
    for (int i = 0; i < wellPast; ++i) parens += "(";
    parens += "1";
    for (int i = 0; i < wellPast; ++i) parens += ")";
    CHECK(!parseNumberExpression(parens).ok);

    std::string calls;
    for (int i = 0; i < wellPast; ++i) calls += "abs(";
    calls += "1";
    for (int i = 0; i < wellPast; ++i) calls += ")";
    CHECK(!parseNumberExpression(calls).ok);

    const std::string unterminated(static_cast<std::size_t>(wellPast), '(');
    CHECK(!parseNumberExpression(unterminated).ok);

    std::string negate(static_cast<std::size_t>(wellPast), '-');
    negate += "self.x";
    CHECK(!parseNumberExpression(negate).ok);

    // Regression guard: nesting comfortably under both the AST depth budget
    // (16) and the new grammar cap (64) parsed before this change and must
    // still parse.
    std::string shallow;
    for (int i = 0; i < 10; ++i) shallow += "abs(";
    shallow += "1";
    for (int i = 0; i < 10; ++i) shallow += ")";
    CHECK(parseNumberExpression(shallow).ok);
}

/**
 * ADR-0038 Finding 3: `compileNumberExpressionToLua` must fail with its own
 * error on an untranslatable tree instead of leaving the caller to invent a
 * fallback. Validation rejects a null child as NE_INCOMPLETE before this can
 * be reached through a real board, so this exercises the compiler directly —
 * the primitive `emitGuardedVec2` depends on to refuse rather than emit `0`.
 */
void testCompilerRejectsIncompleteExpression() {
    NumberUnaryExpression negate;
    negate.operation = NumberUnaryOperator::Negate;
    negate.operand = nullptr;
    const CompiledNumberExpression result =
        compileNumberExpressionToLua(NumberExpression{std::move(negate)});
    CHECK(!result.ok);
    CHECK(!result.error.empty());
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

/**
 * The completion list is the only way an author discovers the vocabulary now
 * that the palette is gone, so a name the grammar accepts but the list omits is
 * effectively unreachable. Every completion must also parse.
 */
void testCompletionsCoverTheGrammar() {
    const auto& entries = numberExpressionCompletions();
    CHECK(!entries.empty());
    for (const NumberExpressionCompletion& entry : entries) {
        CHECK(!entry.label.empty());
        CHECK(!entry.summary.empty());
        // A function insert ends at the open paren; complete it and it must
        // parse, which catches an entry whose spelling drifted from the parser.
        std::string probe = entry.insert;
        if (!probe.empty() && probe.back() == '(') {
            const std::string name = probe.substr(0, probe.size() - 1);
            const std::size_t arity =
                (name == "clamp" || name == "lerp") ? 3
                : (name == "random" || name == "min" || name == "max") ? 2 : 1;
            for (std::size_t i = 0; i < arity; ++i) probe += (i ? ", 1" : "1");
            probe += ")";
        }
        const NumberExpressionParseResult parsed = parseNumberExpression(probe);
        if (parsed.ok) { ++passed; continue; }
        ++failed;
        std::cerr << "FAIL completion '" << entry.insert << "' does not parse as '"
                  << probe << "' — " << parsed.error.message << "\n";
    }
    // Every callable name the parser knows has to be offered.
    for (const char* name : {"abs", "floor", "ceil", "round", "min", "max",
                             "random", "clamp", "lerp"}) {
        const std::string insert = std::string(name) + "(";
        bool offered = false;
        for (const NumberExpressionCompletion& entry : entries)
            if (entry.insert == insert) offered = true;
        if (offered) { ++passed; continue; }
        ++failed;
        std::cerr << "FAIL '" << name << "' parses but is not in the completion list\n";
    }
}

/**
 * The token rule decides what a completion replaces, which is why it lives with
 * the parser instead of being re-derived in the editor. An empty prefix means
 * "not typing a name yet" — the caller shows the whole vocabulary, which is the
 * ADR's requirement that the list be reachable without knowing what to type.
 */
void testCompletionTokenRule() {
    CHECK(numberExpressionTokenPrefix("") == "");
    CHECK(numberExpressionTokenPrefix("ra") == "ra");
    CHECK(numberExpressionTokenPrefix("clamp(self.") == "self.");
    CHECK(numberExpressionTokenPrefix("random(0, ") == "");
    CHECK(numberExpressionTokenPrefix("$sc") == "$sc");
    CHECK(numberExpressionTokenPrefix("1 + ab") == "ab");
    // ADR-0038 minor finding 7: an unterminated quoted variable name can
    // contain a space, which used to end the token scan early and lose the
    // `$'my ` prefix entirely.
    CHECK(numberExpressionTokenPrefix("$'my ") == "$'my ");
    CHECK(numberExpressionTokenPrefix("clamp($'a b, 0, 1") == "$'a b, 0, 1");
    // A properly closed quoted variable leaves no token in progress.
    CHECK(numberExpressionTokenPrefix("$'a' ") == "");
    CHECK(numberExpressionTokenPrefix("$'a' + ") == "");

    CHECK(applyNumberExpressionCompletion("cl", "clamp(") == "clamp(");
    CHECK(applyNumberExpressionCompletion("random(0, sc", "scene.width")
          == "random(0, scene.width");
    CHECK(applyNumberExpressionCompletion("", "self.x") == "self.x");
    // Nothing typed yet: the completion is appended, not swallowing an operator.
    CHECK(applyNumberExpressionCompletion("1 + ", "self.x") == "1 + self.x");
}

/**
 * Anything offered to the author has to be text they can actually commit. The
 * editor used to build `"$" + key` by hand, which is correct until a variable
 * is named with a space and the completion inserts something unparseable.
 */
void testVariableTokensAreAlwaysParseable() {
    for (const char* name : {"score", "has spaces", "it's odd", "9lives", ""}) {
        for (const NumberVariableScope scope :
             {NumberVariableScope::Local, NumberVariableScope::Global}) {
            const std::string token = numberExpressionVariableToken(scope, name);
            CHECK(!token.empty());
            // ADR-0038 minor finding 6: an empty name is not offerable through
            // the editor, but the token it would produce (`$''`) must still
            // round-trip — `parse(format(e, Code)) == e` holds for every
            // expression, not every expression except this one.
            const NumberExpressionParseResult parsed = parseNumberExpression(token);
            if (!parsed.ok) {
                ++failed;
                std::cerr << "FAIL variable token '" << token << "' does not parse — "
                          << parsed.error.message << "\n";
                continue;
            }
            const auto* node =
                std::get_if<NumberVariableExpression>(&parsed.value.value());
            if (node && node->scope == scope && node->variableId == name) { ++passed; continue; }
            ++failed;
            std::cerr << "FAIL variable token '" << token << "' round-tripped wrong\n";
        }
    }
}

} // namespace

int main() {
    testRoundTrip();
    testAuthoredText();
    testRejects();
    testLimits();
    testLiteralFidelity();
    testParseRecursionBounded();
    testCompilerRejectsIncompleteExpression();
    testCompletionsCoverTheGrammar();
    testCompletionTokenRule();
    testVariableTokensAreAlwaysParseable();
    std::cout << "number-expression-syntax-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
