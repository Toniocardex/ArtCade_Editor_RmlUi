#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ArtCade::Scripts {
struct ScriptApiEntry;
}

namespace ArtCade::EditorNative {

struct ScriptLanguageHint {
    std::string title;
    std::string detail;
    std::string insertText;
    std::string qualifiedName;
};

struct ScriptCompletionEdit {
    std::string text;
    std::size_t selectionBegin = 0;
    std::size_t selectionEnd = 0;
};

struct ScriptApiCatalogInsertResult {
    bool applied = false;
    std::string message;
    ScriptCompletionEdit edit;
};

// Pure language-service projections over Scripts::scriptApiCatalogV1().
// Not buffer authority; callers apply inserts via EditScriptBufferIntent.

std::string scriptIdentifierAt(const std::string& text, std::size_t cursorOffset);

std::string scriptDottedPathAt(const std::string& text, std::size_t cursorOffset);

// Trailing identifier span only (never crosses '.'). Empty when cursor is not
// on/just after an identifier, or when that identifier is a Lua reserved word
// that must not be completion-replaced (e.g. "end").
bool scriptTrailingIdentSpan(const std::string& text, std::size_t cursorOffset,
                             std::size_t& begin, std::size_t& end);

const Scripts::ScriptApiEntry* resolveScriptApiAt(const std::string& text,
                                                  std::size_t cursorOffset);

ScriptLanguageHint scriptHoverAt(const std::string& text, std::size_t cursorOffset);

ScriptLanguageHint scriptSignatureAt(const std::string& text, std::size_t cursorOffset);

// Context-aware insert text for a catalog entry at the caret (methods keep the
// dotted receiver; multi-line snippets are returned intact).
std::string scriptInsertTextFor(const Scripts::ScriptApiEntry& entry,
                                const std::string& text,
                                std::size_t cursorOffset);

// Apply an insert: replaces only a safe trailing identifier, otherwise inserts
// at the caret. Never eats a dotted path or a reserved word like "end".
// Multi-line snippets get a leading and trailing newline when needed so
// adjacent tokens are not glued (e.g. end + on_update → end\non_update).
ScriptCompletionEdit applyScriptCompletionInsert(const std::string& text,
                                                 std::size_t cursorOffset,
                                                 const std::string& insertText);

// Panel / catalog insert routed by ScriptApiInsertKind: lifecycle callbacks go
// into return { }, API declarations once at file head, calls/expressions at
// the caret. None → documentation only (no buffer mutation).
ScriptApiCatalogInsertResult applyScriptApiCatalogInsert(
    const Scripts::ScriptApiEntry& entry,
    const std::string& text,
    std::size_t cursorOffset);

std::vector<ScriptLanguageHint> scriptCompletionsAt(const std::string& text,
                                                    std::size_t cursorOffset,
                                                    std::size_t limit = 24);

std::vector<ScriptLanguageHint> scriptApiReferenceEntries();

} // namespace ArtCade::EditorNative
