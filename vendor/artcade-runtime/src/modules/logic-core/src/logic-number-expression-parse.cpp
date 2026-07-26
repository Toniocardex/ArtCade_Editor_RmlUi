#include "logic-number-expression-parse.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::Logic {
namespace {

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isDigitOrDot(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.';
}

struct FunctionSpec {
    const char* name;
    std::size_t arity;
};

const std::vector<FunctionSpec>& functions() {
    static const std::vector<FunctionSpec> specs{
        {"abs", 1},   {"floor", 1}, {"ceil", 1},   {"round", 1},
        {"min", 2},   {"max", 2},   {"random", 2},
        {"clamp", 3}, {"lerp", 3},
    };
    return specs;
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    NumberExpressionParseResult run() {
        NumberExpressionParseResult result;
        skipSpace();
        if (eof()) {
            result.error = {"Enter a number or an expression", pos_};
            return result;
        }
        NumberExpression value = parseExpression(0);
        if (failed_) {
            result.error = error_;
            return result;
        }
        skipSpace();
        if (!eof()) {
            result.error = {"Unexpected '" + std::string(1, peek()) + "'", pos_};
            return result;
        }
        result.ok = true;
        result.value = std::move(value);
        return result;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
    bool failed_ = false;
    NumberExpressionParseError error_;

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return eof() ? '\0' : text_[pos_]; }

    void skipSpace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) ++pos_;
    }

    NumberExpression fail(const std::string& message, std::size_t offset) {
        if (!failed_) {
            failed_ = true;
            error_ = {message, offset};
        }
        return NumberExpression::literal(0.0);
    }

    /** Every constructed node passes through here, so one budget covers all. */
    NumberExpression make(NumberExpressionVariant value, int depth) {
        if (depth > static_cast<int>(kMaximumNumberExpressionDepth))
            return fail("Expression is nested too deeply", pos_);
        if (++nodes_ > kMaximumNumberExpressionNodes)
            return fail("Expression has too many parts", pos_);
        return NumberExpression{std::move(value)};
    }

    bool consume(char c) {
        skipSpace();
        if (peek() != c) return false;
        ++pos_;
        return true;
    }

    NumberExpression parseExpression(int depth) {
        NumberExpression left = parseTerm(depth);
        while (!failed_) {
            skipSpace();
            const char c = peek();
            if (c != '+' && c != '-') break;
            ++pos_;
            NumberExpression right = parseTerm(depth + 1);
            if (failed_) break;
            NumberBinaryExpression node;
            node.operation = c == '+' ? NumberBinaryOperator::Add
                                      : NumberBinaryOperator::Subtract;
            node.left = boxNumberExpression(std::move(left));
            node.right = boxNumberExpression(std::move(right));
            left = make(std::move(node), depth);
        }
        return left;
    }

    NumberExpression parseTerm(int depth) {
        NumberExpression left = parseFactor(depth);
        while (!failed_) {
            skipSpace();
            const char c = peek();
            if (c != '*' && c != '/') break;
            ++pos_;
            NumberExpression right = parseFactor(depth + 1);
            if (failed_) break;
            NumberBinaryExpression node;
            node.operation = c == '*' ? NumberBinaryOperator::Multiply
                                      : NumberBinaryOperator::Divide;
            node.left = boxNumberExpression(std::move(left));
            node.right = boxNumberExpression(std::move(right));
            left = make(std::move(node), depth);
        }
        return left;
    }

    NumberExpression parseFactor(int depth) {
        skipSpace();
        if (peek() != '-') return parsePrimary(depth);
        ++pos_;
        skipSpace();
        // `-5` is the literal -5, not Negate(5). Without this fold the
        // formatter's `-5` would not round-trip, which is also why the Code
        // style always parenthesises a real Negate as `-(…)`.
        if (isDigitOrDot(peek())) {
            const NumberExpression literal = parseNumber(depth);
            if (failed_) return literal;
            const auto value = literalNumberValue(literal);
            return make(NumberLiteralExpression{value ? -*value : 0.0}, depth);
        }
        NumberExpression operand = parseFactor(depth + 1);
        if (failed_) return operand;
        NumberUnaryExpression node;
        node.operation = NumberUnaryOperator::Negate;
        node.operand = boxNumberExpression(std::move(operand));
        return make(std::move(node), depth);
    }

    NumberExpression parseNumber(int depth) {
        const std::size_t start = pos_;
        while (!eof() && isDigitOrDot(peek())) ++pos_;
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            const std::size_t mark = pos_;
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                while (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) ++pos_;
            } else {
                pos_ = mark;  // a bare 'e' is not part of this number
            }
        }
        const std::string text = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size())
            return fail("'" + text + "' is not a number", start);
        if (!isFiniteNumberLiteral(value))
            return fail("Number is out of range", start);
        return make(NumberLiteralExpression{value}, depth);
    }

    /** Dotted name: `self.x`, `scene.width`, `delta`, `clamp`. */
    std::string parseName() {
        const std::size_t start = pos_;
        while (!eof() && (isIdentifierChar(peek()) || peek() == '.')) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    NumberExpression parseVariable(int depth) {
        const std::size_t start = pos_;
        ++pos_;  // '$'
        NumberVariableScope scope = NumberVariableScope::Local;
        if (text_.compare(pos_, 7, "global.") == 0) {
            scope = NumberVariableScope::Global;
            pos_ += 7;
        }
        std::string name;
        if (peek() == '\'') {
            ++pos_;
            while (!eof() && peek() != '\'') {
                if (peek() == '\\' && pos_ + 1 < text_.size()) ++pos_;
                name += text_[pos_++];
            }
            if (eof()) return fail("Unterminated variable name", start);
            ++pos_;  // closing quote
        } else {
            while (!eof() && isIdentifierChar(peek())) name += text_[pos_++];
        }
        if (name.empty()) return fail("Expected a variable name after '$'", start);
        NumberVariableExpression node;
        node.scope = scope;
        node.variableId = std::move(name);
        return make(std::move(node), depth);
    }

    NumberExpression parseCall(const std::string& name, std::size_t nameStart, int depth) {
        const FunctionSpec* spec = nullptr;
        for (const FunctionSpec& candidate : functions())
            if (name == candidate.name) spec = &candidate;
        if (!spec) return fail("Unknown function '" + name + "'", nameStart);

        std::vector<NumberExpression> args;
        if (!consume('(')) return fail("Expected '(' after '" + name + "'", pos_);
        if (!consume(')')) {
            while (true) {
                args.push_back(parseExpression(depth + 1));
                if (failed_) return NumberExpression::literal(0.0);
                if (consume(',')) continue;
                if (consume(')')) break;
                return fail("Expected ',' or ')' in '" + name + "'", pos_);
            }
        }
        if (args.size() != spec->arity) {
            return fail(name + " takes " + std::to_string(spec->arity)
                        + (spec->arity == 1 ? " value, got " : " values, got ")
                        + std::to_string(args.size()), nameStart);
        }

        const auto box = [&](std::size_t index) {
            return boxNumberExpression(std::move(args[index]));
        };
        if (name == "clamp") {
            NumberClampExpression node;
            node.value = box(0);
            node.minimum = box(1);
            node.maximum = box(2);
            return make(std::move(node), depth);
        }
        if (name == "lerp") {
            NumberLerpExpression node;
            node.from = box(0);
            node.to = box(1);
            node.amount = box(2);
            return make(std::move(node), depth);
        }
        if (name == "random") {
            NumberRandomRangeExpression node;
            node.minimum = box(0);
            node.maximum = box(1);
            return make(std::move(node), depth);
        }
        if (name == "min" || name == "max") {
            NumberBinaryExpression node;
            node.operation = name == "min" ? NumberBinaryOperator::Minimum
                                           : NumberBinaryOperator::Maximum;
            node.left = box(0);
            node.right = box(1);
            return make(std::move(node), depth);
        }
        NumberUnaryExpression node;
        if (name == "abs") node.operation = NumberUnaryOperator::Absolute;
        else if (name == "floor") node.operation = NumberUnaryOperator::Floor;
        else if (name == "ceil") node.operation = NumberUnaryOperator::Ceil;
        else node.operation = NumberUnaryOperator::Round;
        node.operand = box(0);
        return make(std::move(node), depth);
    }

    NumberExpression parsePrimary(int depth) {
        skipSpace();
        if (eof()) return fail("Expression is incomplete", pos_);
        const char c = peek();
        if (c == '(') {
            ++pos_;
            NumberExpression inner = parseExpression(depth);
            if (failed_) return inner;
            if (!consume(')')) return fail("Expected ')'", pos_);
            return inner;
        }
        if (isDigitOrDot(c)) return parseNumber(depth);
        if (c == '$') return parseVariable(depth);
        if (isIdentifierStart(c)) {
            const std::size_t start = pos_;
            const std::string name = parseName();
            if (name == "self.x")
                return make(NumberPropertyExpression{NumberProperty::SelfPositionX}, depth);
            if (name == "self.y")
                return make(NumberPropertyExpression{NumberProperty::SelfPositionY}, depth);
            if (name == "scene.width")
                return make(NumberPropertyExpression{NumberProperty::SceneWorldWidth}, depth);
            if (name == "scene.height")
                return make(NumberPropertyExpression{NumberProperty::SceneWorldHeight}, depth);
            if (name == "delta")
                return make(NumberPropertyExpression{NumberProperty::DeltaSeconds}, depth);
            return parseCall(name, start, depth);
        }
        return fail("Unexpected '" + std::string(1, c) + "'", pos_);
    }
};

} // namespace

NumberExpressionParseResult parseNumberExpression(const std::string& text) {
    return Parser{text}.run();
}

const std::vector<NumberExpressionCompletion>& numberExpressionCompletions() {
    static const std::vector<NumberExpressionCompletion> entries{
        {"self.x", "self.x", "This object's world X"},
        {"self.y", "self.y", "This object's world Y"},
        {"scene.width", "scene.width", "Scene world width"},
        {"scene.height", "scene.height", "Scene world height"},
        {"delta", "delta", "Seconds since the last frame"},
        {"random(", "random(min, max)", "A new value each time it runs"},
        {"clamp(", "clamp(value, min, max)", "Keep a value inside a range"},
        {"lerp(", "lerp(from, to, amount)", "Blend between two values"},
        {"min(", "min(a, b)", "The smaller of two values"},
        {"max(", "max(a, b)", "The larger of two values"},
        {"abs(", "abs(value)", "Distance from zero"},
        {"floor(", "floor(value)", "Round down"},
        {"ceil(", "ceil(value)", "Round up"},
        {"round(", "round(value)", "Round to the nearest whole number"},
    };
    return entries;
}

} // namespace ArtCade::Logic
