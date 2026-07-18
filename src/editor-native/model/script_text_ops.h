#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

inline constexpr std::size_t kScriptIndentSpaces = 4;

struct ScriptTextEditResult {
    std::string text;
    std::size_t selectionBegin = 0;
    std::size_t selectionEnd = 0;
};

// Pure Lua-oriented text ops. ScriptEditorBuffer remains the authority; the
// surface/controller applies these results through EditScriptBufferIntent.

ScriptTextEditResult insertSpacesAtSelection(const std::string& text,
                                             std::size_t selectionBegin,
                                             std::size_t selectionEnd,
                                             std::size_t spaceCount = kScriptIndentSpaces);

// Tab with a multi-line selection indents every touched line; otherwise inserts spaces.
ScriptTextEditResult indentOrInsertTab(const std::string& text,
                                       std::size_t selectionBegin,
                                       std::size_t selectionEnd);

ScriptTextEditResult outdentSelection(const std::string& text,
                                      std::size_t selectionBegin,
                                      std::size_t selectionEnd);

// Toggle "-- " line comments for every line touching the selection.
ScriptTextEditResult toggleLineComment(const std::string& text,
                                       std::size_t selectionBegin,
                                       std::size_t selectionEnd);

// Enter: insert '\n' plus indentation copied from the previous line (plus one
// level after an unmatched '{', '(', or '[' on that line).
ScriptTextEditResult insertNewlineWithAutoIndent(const std::string& text,
                                                 std::size_t cursorOffset);

// Matching bracket at or before cursorOffset, or nullopt.
std::optional<std::pair<std::size_t, std::size_t>> findMatchingBracket(
    const std::string& text, std::size_t cursorOffset);

// Duplicate every line touching the selection (VS-style block duplicate).
ScriptTextEditResult duplicateSelectionLines(const std::string& text,
                                             std::size_t selectionBegin,
                                             std::size_t selectionEnd);

// Move selection lines up (direction < 0) or down (direction > 0).
ScriptTextEditResult moveSelectionLines(const std::string& text,
                                        std::size_t selectionBegin,
                                        std::size_t selectionEnd,
                                        int direction);

enum class LuaTokenKind {
    Text,
    Keyword,
    Identifier,
    Number,
    String,
    Comment,
    Operator,
};

struct LuaTokenSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    LuaTokenKind kind = LuaTokenKind::Text;
};

std::vector<LuaTokenSpan> tokenizeLua(const std::string& text);

// Optional decorations painted into the highlight overlay (not buffer authority).
struct ScriptHighlightDecorations {
    std::vector<int> errorLines; // 1-based
    int currentLine = 0;         // 1-based; 0 = none
    // Inclusive byte offsets of a matching bracket pair.
    std::optional<std::pair<std::size_t, std::size_t>> bracketMatch;
};

// Rml-safe highlighted markup for a highlight overlay (same monospace metrics).
std::string highlightLuaToRml(const std::string& text,
                              const ScriptHighlightDecorations& decorations = {});

} // namespace ArtCade::EditorNative
