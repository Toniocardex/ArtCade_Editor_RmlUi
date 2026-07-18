#include "editor-native/model/script_text_ops.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace ArtCade::EditorNative {
namespace {

std::size_t clampOffset(const std::string& text, std::size_t offset) {
    return std::min(offset, text.size());
}

std::size_t lineStart(const std::string& text, std::size_t offset) {
    offset = clampOffset(text, offset);
    while (offset > 0 && text[offset - 1] != '\n') --offset;
    return offset;
}

std::size_t lineEnd(const std::string& text, std::size_t offset) {
    offset = clampOffset(text, offset);
    while (offset < text.size() && text[offset] != '\n') ++offset;
    return offset;
}

bool selectionSpansMultipleLines(const std::string& text,
                                 std::size_t begin, std::size_t end) {
    begin = clampOffset(text, begin);
    end = clampOffset(text, end);
    if (end < begin) std::swap(begin, end);
    if (begin == end) return false;
    return text.find('\n', begin) < end;
}

std::size_t leadingSpaces(const std::string& text, std::size_t start, std::size_t end) {
    std::size_t count = 0;
    for (std::size_t i = start; i < end && text[i] == ' '; ++i) ++count;
    return count;
}

bool lineIsCommented(const std::string& text, std::size_t start, std::size_t end) {
    std::size_t i = start;
    while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
    return i + 1 < end && text[i] == '-' && text[i + 1] == '-';
}

std::string_view lineContent(const std::string& text, std::size_t start, std::size_t end) {
    std::size_t i = start;
    while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
    return std::string_view(text.data() + i, end - i);
}

bool isIdentStart(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_';
}

bool isIdentCont(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

const std::unordered_set<std::string_view>& luaKeywords() {
    static const std::unordered_set<std::string_view> k = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    };
    return k;
}

char matchingOpen(char close) {
    switch (close) {
    case ')': return '(';
    case ']': return '[';
    case '}': return '{';
    default: return 0;
    }
}

char matchingClose(char open) {
    switch (open) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    default: return 0;
    }
}

bool isBracket(char c) {
    return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

std::string escapeForRml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

const char* classForToken(LuaTokenKind kind) {
    switch (kind) {
    case LuaTokenKind::Keyword: return "lua-kw";
    case LuaTokenKind::String: return "lua-str";
    case LuaTokenKind::Comment: return "lua-com";
    case LuaTokenKind::Number: return "lua-num";
    case LuaTokenKind::Operator: return "lua-op";
    case LuaTokenKind::Identifier: return "lua-id";
    case LuaTokenKind::Text: return nullptr;
    }
    return nullptr;
}

// Apply a per-line transform over [firstLineStart, lastLineEnd].
template <class Fn>
ScriptTextEditResult mapLines(const std::string& text,
                              std::size_t selectionBegin,
                              std::size_t selectionEnd,
                              Fn&& transformLine) {
    std::size_t begin = clampOffset(text, selectionBegin);
    std::size_t end = clampOffset(text, selectionEnd);
    if (end < begin) std::swap(begin, end);
    const std::size_t first = lineStart(text, begin);
    std::size_t last = lineEnd(text, begin == end ? begin : (end > begin ? end - 1 : end));
    if (begin != end && end > 0 && text[end - 1] == '\n' && end - 1 >= begin) {
        last = lineEnd(text, lineStart(text, end > 1 ? end - 2 : 0));
    }

    ScriptTextEditResult out;
    out.text.append(text, 0, first);
    std::size_t deltaBefore = 0;
    std::size_t deltaTotal = 0;
    std::size_t cursor = first;
    while (true) {
        const std::size_t start = cursor;
        const std::size_t finish = lineEnd(text, start);
        std::string line(text.begin() + static_cast<std::ptrdiff_t>(start),
                         text.begin() + static_cast<std::ptrdiff_t>(finish));
        const std::size_t before = line.size();
        transformLine(line);
        const std::ptrdiff_t delta =
            static_cast<std::ptrdiff_t>(line.size()) - static_cast<std::ptrdiff_t>(before);
        if (start < begin) deltaBefore += static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, delta));
        // Track signed deltas carefully for shrink cases via total rewrite of selection:
        deltaTotal = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(deltaTotal) + delta);
        // Actually keep running signed delta:
        out.text += line;
        if (finish >= last) {
            out.text.append(text, finish, text.size() - finish);
            break;
        }
        out.text.push_back('\n');
        cursor = finish + 1;
    }
    // Recompute selection using size difference of the rewritten region.
    const std::size_t oldRegion = last - first;
    const std::size_t newRegion = out.text.size() - (text.size() - last) - first;
    const std::ptrdiff_t regionDelta =
        static_cast<std::ptrdiff_t>(newRegion) - static_cast<std::ptrdiff_t>(oldRegion);
    (void)deltaBefore;
    (void)deltaTotal;
    (void)regionDelta;
    // Simpler selection: keep offsets relative to rewritten block by scanning.
    // Fall back: place caret at end of edit for ops that compute explicitly below.
    out.selectionBegin = begin;
    out.selectionEnd = end;
    return out;
}

} // namespace

ScriptTextEditResult insertSpacesAtSelection(const std::string& text,
                                             std::size_t selectionBegin,
                                             std::size_t selectionEnd,
                                             std::size_t spaceCount) {
    std::size_t begin = clampOffset(text, selectionBegin);
    std::size_t end = clampOffset(text, selectionEnd);
    if (end < begin) std::swap(begin, end);
    ScriptTextEditResult out;
    out.text = text;
    out.text.replace(begin, end - begin, spaceCount, ' ');
    out.selectionBegin = begin + spaceCount;
    out.selectionEnd = out.selectionBegin;
    return out;
}

ScriptTextEditResult indentOrInsertTab(const std::string& text,
                                       std::size_t selectionBegin,
                                       std::size_t selectionEnd) {
    std::size_t begin = clampOffset(text, selectionBegin);
    std::size_t end = clampOffset(text, selectionEnd);
    if (end < begin) std::swap(begin, end);
    if (!selectionSpansMultipleLines(text, begin, end)) {
        return insertSpacesAtSelection(text, begin, end, kScriptIndentSpaces);
    }

    const std::size_t first = lineStart(text, begin);
    const std::size_t last = lineEnd(text, end > begin ? end - 1 : end);

    ScriptTextEditResult out;
    out.text.append(text, 0, first);
    std::size_t addedBefore = 0;
    std::size_t addedTotal = 0;
    std::size_t cursor = first;
    while (true) {
        const std::size_t start = cursor;
        const std::size_t finish = lineEnd(text, start);
        if (start < begin) addedBefore += kScriptIndentSpaces;
        addedTotal += kScriptIndentSpaces;
        out.text.append(kScriptIndentSpaces, ' ');
        out.text.append(text, start, finish - start);
        if (finish >= last) {
            out.text.append(text, finish, text.size() - finish);
            break;
        }
        out.text.push_back('\n');
        cursor = finish + 1;
    }
    out.selectionBegin = begin + addedBefore;
    out.selectionEnd = end + addedTotal;
    return out;
}

ScriptTextEditResult outdentSelection(const std::string& text,
                                      std::size_t selectionBegin,
                                      std::size_t selectionEnd) {
    std::size_t begin = clampOffset(text, selectionBegin);
    std::size_t end = clampOffset(text, selectionEnd);
    if (end < begin) std::swap(begin, end);
    const std::size_t first = lineStart(text, begin);
    const std::size_t last = lineEnd(text, begin == end ? begin : (end > begin ? end - 1 : end));

    ScriptTextEditResult out;
    out.text.append(text, 0, first);
    std::size_t removedBefore = 0;
    std::size_t removedTotal = 0;
    std::size_t cursor = first;
    while (true) {
        const std::size_t start = cursor;
        const std::size_t finish = lineEnd(text, start);
        const std::size_t spaces =
            std::min(kScriptIndentSpaces, leadingSpaces(text, start, finish));
        if (start < begin) removedBefore += spaces;
        removedTotal += spaces;
        out.text.append(text, start + spaces, finish - (start + spaces));
        if (finish >= last) {
            out.text.append(text, finish, text.size() - finish);
            break;
        }
        out.text.push_back('\n');
        cursor = finish + 1;
    }
    out.selectionBegin = begin - std::min(begin, removedBefore);
    out.selectionEnd = end - std::min(end, removedTotal);
    if (out.selectionEnd < out.selectionBegin) out.selectionEnd = out.selectionBegin;
    return out;
}

ScriptTextEditResult toggleLineComment(const std::string& text,
                                       std::size_t selectionBegin,
                                       std::size_t selectionEnd) {
    std::size_t begin = clampOffset(text, selectionBegin);
    std::size_t end = clampOffset(text, selectionEnd);
    if (end < begin) std::swap(begin, end);
    const std::size_t first = lineStart(text, begin);
    std::size_t last = lineEnd(text, begin == end ? begin : (end > begin ? end - 1 : end));
    if (begin != end && end > 0 && text[end - 1] == '\n' && end > begin + 1) {
        last = lineEnd(text, lineStart(text, end - 2));
    }

    bool allCommented = true;
    bool anyContent = false;
    for (std::size_t cursor = first; ; ) {
        const std::size_t start = cursor;
        const std::size_t finish = lineEnd(text, start);
        if (!lineContent(text, start, finish).empty()) {
            anyContent = true;
            if (!lineIsCommented(text, start, finish)) allCommented = false;
        }
        if (finish >= last) break;
        cursor = finish + 1;
    }
    if (!anyContent) allCommented = false;

    ScriptTextEditResult out;
    out.text.append(text, 0, first);
    std::ptrdiff_t signedBefore = 0;
    std::ptrdiff_t signedTotal = 0;
    for (std::size_t cursor = first; ; ) {
        const std::size_t start = cursor;
        const std::size_t finish = lineEnd(text, start);
        std::string line(text.begin() + static_cast<std::ptrdiff_t>(start),
                         text.begin() + static_cast<std::ptrdiff_t>(finish));
        std::ptrdiff_t delta = 0;
        if (allCommented) {
            std::size_t indent = 0;
            while (indent < line.size()
                   && (line[indent] == ' ' || line[indent] == '\t')) {
                ++indent;
            }
            if (indent + 1 < line.size() && line[indent] == '-' && line[indent + 1] == '-') {
                std::size_t remove = 2;
                if (indent + 2 < line.size() && line[indent + 2] == ' ') remove = 3;
                line.erase(indent, remove);
                delta = -static_cast<std::ptrdiff_t>(remove);
            }
        } else {
            std::size_t indent = 0;
            while (indent < line.size()
                   && (line[indent] == ' ' || line[indent] == '\t')) {
                ++indent;
            }
            const bool blank = (indent == line.size());
            if (!blank || begin == end) {
                line.insert(indent, "-- ");
                delta = 3;
            }
        }
        if (start < begin) signedBefore += delta;
        signedTotal += delta;
        out.text += line;
        if (finish >= last) {
            out.text.append(text, finish, text.size() - finish);
            break;
        }
        out.text.push_back('\n');
        cursor = finish + 1;
    }

    const auto applyDelta = [](std::size_t offset, std::ptrdiff_t delta) {
        if (delta >= 0) return offset + static_cast<std::size_t>(delta);
        const std::size_t shrink = static_cast<std::size_t>(-delta);
        return offset > shrink ? offset - shrink : 0;
    };
    out.selectionBegin = applyDelta(begin, signedBefore);
    out.selectionEnd = applyDelta(end, signedTotal);
    if (out.selectionEnd < out.selectionBegin) out.selectionEnd = out.selectionBegin;
    return out;
}

ScriptTextEditResult insertNewlineWithAutoIndent(const std::string& text,
                                                 std::size_t cursorOffset) {
    cursorOffset = clampOffset(text, cursorOffset);
    const std::size_t start = lineStart(text, cursorOffset);
    const std::size_t indent = leadingSpaces(text, start, cursorOffset);
    std::size_t extra = 0;
    if (cursorOffset > start) {
        const char prev = text[cursorOffset - 1];
        if (prev == '{' || prev == '(' || prev == '[') extra = kScriptIndentSpaces;
    }
    ScriptTextEditResult out;
    out.text = text;
    const std::string insertion = "\n" + std::string(indent + extra, ' ');
    out.text.insert(cursorOffset, insertion);
    out.selectionBegin = cursorOffset + insertion.size();
    out.selectionEnd = out.selectionBegin;
    return out;
}

std::optional<std::pair<std::size_t, std::size_t>> findMatchingBracket(
    const std::string& text, std::size_t cursorOffset) {
    if (text.empty()) return std::nullopt;
    cursorOffset = clampOffset(text, cursorOffset);
    std::size_t pos = cursorOffset;
    if (pos < text.size() && isBracket(text[pos])) {
        // keep
    } else if (pos > 0 && isBracket(text[pos - 1])) {
        pos = pos - 1;
    } else {
        return std::nullopt;
    }
    const char ch = text[pos];
    if (const char open = matchingOpen(ch)) {
        int depth = 0;
        for (std::size_t i = pos; i > 0; ) {
            --i;
            if (text[i] == ch) ++depth;
            else if (text[i] == open) {
                if (depth == 0) return std::make_pair(i, pos);
                --depth;
            }
        }
    } else if (const char close = matchingClose(ch)) {
        int depth = 0;
        for (std::size_t i = pos + 1; i < text.size(); ++i) {
            if (text[i] == ch) ++depth;
            else if (text[i] == close) {
                if (depth == 0) return std::make_pair(pos, i);
                --depth;
            }
        }
    }
    return std::nullopt;
}

namespace {

// Inclusive content range for every line touching [begin, end), plus trailing '\n'
// when present on the last touched line.
void selectionLineBlock(const std::string& text, std::size_t begin, std::size_t end,
                        std::size_t& blockStart, std::size_t& blockEnd) {
    begin = clampOffset(text, begin);
    end = clampOffset(text, end);
    if (end < begin) std::swap(begin, end);
    blockStart = lineStart(text, begin);
    std::size_t anchor = begin == end ? begin : (end > begin ? end - 1 : end);
    if (begin != end && end > 0 && text[end - 1] == '\n' && end > begin + 1)
        anchor = end - 2;
    blockEnd = lineEnd(text, anchor);
    if (blockEnd < text.size() && text[blockEnd] == '\n') ++blockEnd;
}

} // namespace

ScriptTextEditResult duplicateSelectionLines(const std::string& text,
                                             std::size_t selectionBegin,
                                             std::size_t selectionEnd) {
    std::size_t blockStart = 0, blockEnd = 0;
    selectionLineBlock(text, selectionBegin, selectionEnd, blockStart, blockEnd);
    std::string block = text.substr(blockStart, blockEnd - blockStart);
    if (block.empty() || block.back() != '\n') block.push_back('\n');
    ScriptTextEditResult out;
    out.text = text;
    out.text.insert(blockEnd, block);
    out.selectionBegin = blockEnd;
    out.selectionEnd = blockEnd + block.size();
    if (!block.empty() && block.back() == '\n' && out.selectionEnd > out.selectionBegin)
        --out.selectionEnd;
    return out;
}

ScriptTextEditResult moveSelectionLines(const std::string& text,
                                        std::size_t selectionBegin,
                                        std::size_t selectionEnd,
                                        int direction) {
    if (direction == 0) {
        ScriptTextEditResult out;
        out.text = text;
        out.selectionBegin = clampOffset(text, selectionBegin);
        out.selectionEnd = clampOffset(text, selectionEnd);
        return out;
    }
    std::size_t blockStart = 0, blockEnd = 0;
    selectionLineBlock(text, selectionBegin, selectionEnd, blockStart, blockEnd);
    if (direction < 0) {
        if (blockStart == 0) {
            ScriptTextEditResult out;
            out.text = text;
            out.selectionBegin = clampOffset(text, selectionBegin);
            out.selectionEnd = clampOffset(text, selectionEnd);
            return out;
        }
        const std::size_t prevStart = lineStart(text, blockStart - 1);
        const std::string moved = text.substr(blockStart, blockEnd - blockStart);
        const std::string prev = text.substr(prevStart, blockStart - prevStart);
        ScriptTextEditResult out;
        out.text.reserve(text.size());
        out.text.append(text, 0, prevStart);
        out.text += moved;
        if (!moved.empty() && moved.back() != '\n' && !prev.empty())
            out.text.push_back('\n');
        out.text += prev;
        out.text.append(text, blockEnd, text.size() - blockEnd);
        const std::size_t selDelta = blockStart - prevStart;
        out.selectionBegin = clampOffset(text, selectionBegin) - selDelta;
        out.selectionEnd = clampOffset(text, selectionEnd) - selDelta;
        return out;
    }

    if (blockEnd >= text.size()) {
        ScriptTextEditResult out;
        out.text = text;
        out.selectionBegin = clampOffset(text, selectionBegin);
        out.selectionEnd = clampOffset(text, selectionEnd);
        return out;
    }
    const std::size_t nextEnd = lineEnd(text, blockEnd);
    std::size_t nextBlockEnd = nextEnd;
    if (nextBlockEnd < text.size() && text[nextBlockEnd] == '\n') ++nextBlockEnd;
    const std::string moved = text.substr(blockStart, blockEnd - blockStart);
    const std::string next = text.substr(blockEnd, nextBlockEnd - blockEnd);
    ScriptTextEditResult out;
    out.text.reserve(text.size());
    out.text.append(text, 0, blockStart);
    out.text += next;
    out.text += moved;
    out.text.append(text, nextBlockEnd, text.size() - nextBlockEnd);
    const std::size_t selDelta = next.size();
    out.selectionBegin = clampOffset(text, selectionBegin) + selDelta;
    out.selectionEnd = clampOffset(text, selectionEnd) + selDelta;
    return out;
}

std::vector<LuaTokenSpan> tokenizeLua(const std::string& text) {
    std::vector<LuaTokenSpan> spans;
    std::size_t i = 0;
    const auto push = [&](std::size_t begin, std::size_t end, LuaTokenKind kind) {
        if (end > begin) spans.push_back(LuaTokenSpan{begin, end, kind});
    };
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            const std::size_t begin = i;
            i += 2;
            if (i + 1 < text.size() && text[i] == '[' && text[i + 1] == '[') {
                i += 2;
                while (i + 1 < text.size()
                       && !(text[i] == ']' && text[i + 1] == ']')) {
                    ++i;
                }
                if (i + 1 < text.size()) i += 2;
            } else {
                while (i < text.size() && text[i] != '\n') ++i;
            }
            push(begin, i, LuaTokenKind::Comment);
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t begin = i;
            const char quote = text[i++];
            while (i < text.size() && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < text.size()) i += 2;
                else ++i;
            }
            if (i < text.size()) ++i;
            push(begin, i, LuaTokenKind::String);
            continue;
        }
        if (c == '[' && i + 1 < text.size() && text[i + 1] == '[') {
            const std::size_t begin = i;
            i += 2;
            while (i + 1 < text.size()
                   && !(text[i] == ']' && text[i + 1] == ']')) {
                ++i;
            }
            if (i + 1 < text.size()) i += 2;
            push(begin, i, LuaTokenKind::String);
            continue;
        }
        if (std::isdigit(c) != 0
            || (c == '.' && i + 1 < text.size()
                && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
            const std::size_t begin = i;
            ++i;
            while (i < text.size()) {
                const unsigned char n = static_cast<unsigned char>(text[i]);
                if (std::isdigit(n) != 0 || n == '.' || n == 'x' || n == 'X'
                    || n == 'e' || n == 'E') {
                    ++i;
                    continue;
                }
                if ((n == '+' || n == '-') && i > begin
                    && (text[i - 1] == 'e' || text[i - 1] == 'E')) {
                    ++i;
                    continue;
                }
                break;
            }
            push(begin, i, LuaTokenKind::Number);
            continue;
        }
        if (isIdentStart(c)) {
            const std::size_t begin = i;
            ++i;
            while (i < text.size()
                   && isIdentCont(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            const std::string_view word(text.data() + begin, i - begin);
            push(begin, i,
                 luaKeywords().count(word) ? LuaTokenKind::Keyword
                                           : LuaTokenKind::Identifier);
            continue;
        }
        if (std::isspace(c) != 0) {
            const std::size_t begin = i;
            while (i < text.size()
                   && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
                ++i;
            }
            push(begin, i, LuaTokenKind::Text);
            continue;
        }
        const std::size_t begin = i;
        ++i;
        push(begin, i, LuaTokenKind::Operator);
    }
    return spans;
}

std::string highlightLuaToRml(const std::string& text,
                              const ScriptHighlightDecorations& decorations) {
    std::unordered_set<int> errorLines(decorations.errorLines.begin(),
                                       decorations.errorLines.end());
    const auto isBracketHit = [&](std::size_t index) {
        return decorations.bracketMatch
            && (index == decorations.bracketMatch->first
                || index == decorations.bracketMatch->second);
    };

    std::string html;
    html.reserve(text.size() * 3 + 64);
    html += "<span class=\"lua-hl\">";

    int line = 1;
    const auto openLine = [&]() {
        html += "<span class=\"lua-line";
        if (errorLines.count(line)) html += " lua-line-error";
        if (decorations.currentLine > 0 && line == decorations.currentLine)
            html += " lua-line-current";
        html += "\">";
    };
    const auto closeLine = [&]() { html += "</span>"; };

    openLine();
    const auto emitChar = [&](std::size_t index, char ch) {
        if (ch == '\n') {
            closeLine();
            html += "<br/>";
            ++line;
            openLine();
            return;
        }
        std::string piece = escapeForRml(std::string_view(&ch, 1));
        if (isBracketHit(index)) {
            html += "<span class=\"lua-bracket\">";
            html += piece;
            html += "</span>";
        } else {
            html += piece;
        }
    };

    for (const LuaTokenSpan& span : tokenizeLua(text)) {
        const char* cls = classForToken(span.kind);
        if (cls) {
            html += "<span class=\"";
            html += cls;
            html += "\">";
        }
        for (std::size_t i = span.begin; i < span.end; ++i) {
            const char ch = text[i];
            if (ch == '\n') {
                if (cls) html += "</span>";
                emitChar(i, ch);
                if (cls) {
                    html += "<span class=\"";
                    html += cls;
                    html += "\">";
                }
            } else {
                emitChar(i, ch);
            }
        }
        if (cls) html += "</span>";
    }
    closeLine();
    if (text.empty() || text.back() == '\n') html += "<br/>";
    html += "</span>";
    return html;
}

} // namespace ArtCade::EditorNative
