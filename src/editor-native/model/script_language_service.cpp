#include "editor-native/model/script_language_service.h"

#include "script-api-catalog.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace ArtCade::EditorNative {
namespace {

bool isIdentChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

std::size_t clampOffset(const std::string& text, std::size_t offset) {
    return std::min(offset, text.size());
}

std::string_view slice(const std::string& text, std::size_t begin, std::size_t end) {
    if (end < begin) end = begin;
    begin = clampOffset(text, begin);
    end = clampOffset(text, end);
    return std::string_view(text.data() + begin, end - begin);
}

bool isReservedIdent(std::string_view word) {
    static const std::unordered_set<std::string_view> kReserved = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    };
    return kReserved.count(word) != 0;
}

ScriptLanguageHint fromEntry(const Scripts::ScriptApiEntry& entry,
                             const std::string& insertText) {
    ScriptLanguageHint hint;
    hint.qualifiedName = entry.qualifiedName;
    hint.title = entry.signature && entry.signature[0] ? entry.signature : entry.qualifiedName;
    hint.detail = Scripts::formatScriptApiHover(entry);
    hint.insertText = insertText;
    return hint;
}

const Scripts::ScriptApiEntry* resolveQualifiedOrSuffix(const std::string& path) {
    if (path.empty()) return nullptr;
    if (const Scripts::ScriptApiEntry* exact =
            Scripts::findScriptApiByQualifiedName(path)) {
        return exact;
    }
    // Prefer longest qualified suffix match (e.g. "self.set_rotation").
    const auto& catalog = Scripts::scriptApiCatalogV1();
    const Scripts::ScriptApiEntry* best = nullptr;
    std::size_t bestLen = 0;
    for (const Scripts::ScriptApiEntry& entry : catalog) {
        const std::string qualified = entry.qualifiedName;
        if (qualified == path) return &entry;
        if (path.size() < qualified.size()) continue;
        if (path.compare(path.size() - qualified.size(), qualified.size(), qualified) != 0)
            continue;
        if (path.size() > qualified.size()
            && path[path.size() - qualified.size() - 1] != '.') {
            continue;
        }
        if (qualified.size() > bestLen) {
            best = &entry;
            bestLen = qualified.size();
        }
    }
    if (best) return best;

    // Bare name: only when exactly one catalog entry shares that name.
    const Scripts::ScriptApiEntry* unique = nullptr;
    for (const Scripts::ScriptApiEntry& entry : catalog) {
        if (!entry.name || entry.name != path) continue;
        if (unique) return nullptr;
        unique = &entry;
    }
    return unique;
}

int completionRank(const Scripts::ScriptApiEntry& entry, const std::string& filter) {
    if (filter.empty()) return 3;
    const std::string qualified = entry.qualifiedName;
    const std::string name = entry.name ? entry.name : "";
    if (qualified == filter || name == filter) return 0;
    if (qualified.compare(0, filter.size(), filter) == 0) return 1;
    if (name.compare(0, filter.size(), filter) == 0) return 1;
    if (qualified.find(filter) != std::string::npos || name.find(filter) != std::string::npos)
        return 2;
    return 4;
}

} // namespace

std::string scriptIdentifierAt(const std::string& text, std::size_t cursorOffset) {
    cursorOffset = clampOffset(text, cursorOffset);
    std::size_t begin = cursorOffset;
    while (begin > 0 && isIdentChar(static_cast<unsigned char>(text[begin - 1])))
        --begin;
    std::size_t end = cursorOffset;
    while (end < text.size() && isIdentChar(static_cast<unsigned char>(text[end])))
        ++end;
    return std::string(slice(text, begin, end));
}

std::string scriptDottedPathAt(const std::string& text, std::size_t cursorOffset) {
    cursorOffset = clampOffset(text, cursorOffset);
    std::size_t begin = cursorOffset;
    while (begin > 0) {
        const unsigned char c = static_cast<unsigned char>(text[begin - 1]);
        if (isIdentChar(c) || c == '.') --begin;
        else break;
    }
    std::size_t end = cursorOffset;
    while (end < text.size() && isIdentChar(static_cast<unsigned char>(text[end])))
        ++end;
    std::string path(slice(text, begin, end));
    while (!path.empty() && path.front() == '.') path.erase(path.begin());
    while (!path.empty() && path.back() == '.') path.pop_back();
    return path;
}

bool scriptTrailingIdentSpan(const std::string& text, std::size_t cursorOffset,
                             std::size_t& begin, std::size_t& end) {
    cursorOffset = clampOffset(text, cursorOffset);
    begin = cursorOffset;
    while (begin > 0 && isIdentChar(static_cast<unsigned char>(text[begin - 1])))
        --begin;
    end = cursorOffset;
    while (end < text.size() && isIdentChar(static_cast<unsigned char>(text[end])))
        ++end;
    if (begin == end) return false;
    if (isReservedIdent(slice(text, begin, end))) return false;
    return true;
}

const Scripts::ScriptApiEntry* resolveScriptApiAt(const std::string& text,
                                                  std::size_t cursorOffset) {
    const std::string path = scriptDottedPathAt(text, cursorOffset);
    if (const Scripts::ScriptApiEntry* entry = resolveQualifiedOrSuffix(path))
        return entry;
    const std::string ident = scriptIdentifierAt(text, cursorOffset);
    if (ident != path) return resolveQualifiedOrSuffix(ident);
    return nullptr;
}

ScriptLanguageHint scriptHoverAt(const std::string& text, std::size_t cursorOffset) {
    const Scripts::ScriptApiEntry* entry = resolveScriptApiAt(text, cursorOffset);
    if (!entry) return {};
    return fromEntry(*entry, entry->insertText ? entry->insertText : entry->qualifiedName);
}

ScriptLanguageHint scriptSignatureAt(const std::string& text, std::size_t cursorOffset) {
    cursorOffset = clampOffset(text, cursorOffset);
    std::size_t probe = cursorOffset;
    while (probe > 0 && std::isspace(static_cast<unsigned char>(text[probe - 1])))
        --probe;
    if (probe > 0 && text[probe - 1] == '(') --probe;
    while (probe > 0 && std::isspace(static_cast<unsigned char>(text[probe - 1])))
        --probe;
    const Scripts::ScriptApiEntry* entry = resolveScriptApiAt(text, probe);
    if (!entry) return {};
    ScriptLanguageHint hint;
    hint.qualifiedName = entry->qualifiedName;
    hint.title = Scripts::formatScriptApiSignatureHelp(*entry);
    hint.detail = entry->shortDoc ? entry->shortDoc : "";
    hint.insertText = scriptInsertTextFor(*entry, text, probe);
    return hint;
}

std::string scriptInsertTextFor(const Scripts::ScriptApiEntry& entry,
                                const std::string& text,
                                std::size_t cursorOffset) {
    const std::string path = scriptDottedPathAt(text, cursorOffset);
    if (entry.kind == Scripts::ScriptApiKind::Method && entry.name) {
        const std::string parent = entry.parentPath ? entry.parentPath : "";
        const bool hasReceiver = !parent.empty()
            && (path == parent || path.rfind(parent + ".", 0) == 0);
        if (hasReceiver) {
            std::string insert = entry.name;
            if (entry.paramCount > 0) insert.push_back('(');
            else insert += "()";
            return insert;
        }
        std::string insert = entry.qualifiedName;
        if (entry.paramCount > 0) insert.push_back('(');
        else insert += "()";
        return insert;
    }
    if (entry.insertText && entry.insertText[0] != '\0') return entry.insertText;
    return entry.qualifiedName;
}

ScriptCompletionEdit applyScriptCompletionInsert(const std::string& text,
                                                 std::size_t cursorOffset,
                                                 const std::string& insertText) {
    ScriptCompletionEdit edit;
    cursorOffset = clampOffset(text, cursorOffset);
    if (insertText.empty()) {
        edit.text = text;
        edit.selectionBegin = cursorOffset;
        edit.selectionEnd = cursorOffset;
        return edit;
    }

    const bool multiLine = insertText.find('\n') != std::string::npos;
    std::size_t begin = cursorOffset;
    std::size_t end = cursorOffset;
    const bool replaceIdent = !multiLine
        && scriptTrailingIdentSpan(text, cursorOffset, begin, end);

    if (!replaceIdent) {
        begin = cursorOffset;
        end = cursorOffset;
    }

    std::string prefix;
    if (multiLine && begin > 0 && text[begin - 1] != '\n') prefix = "\n";

    edit.text = text.substr(0, begin) + prefix + insertText + text.substr(end);
    edit.selectionBegin = begin + prefix.size() + insertText.size();
    edit.selectionEnd = edit.selectionBegin;
    return edit;
}

std::vector<ScriptLanguageHint> scriptCompletionsAt(const std::string& text,
                                                    std::size_t cursorOffset,
                                                    std::size_t limit) {
    const std::string prefix = scriptDottedPathAt(text, cursorOffset);
    const std::string ident = scriptIdentifierAt(text, cursorOffset);
    const std::string filter = !prefix.empty() ? prefix : ident;
    struct Ranked {
        int rank = 0;
        ScriptLanguageHint hint;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(Scripts::scriptApiCatalogV1().size());
    for (const Scripts::ScriptApiEntry& entry : Scripts::scriptApiCatalogV1()) {
        const std::string qualified = entry.qualifiedName;
        const std::string name = entry.name ? entry.name : "";
        if (!filter.empty()) {
            const bool match = qualified.find(filter) != std::string::npos
                || name.find(filter) != std::string::npos;
            if (!match) continue;
        }
        ranked.push_back(Ranked{
            completionRank(entry, filter),
            fromEntry(entry, scriptInsertTextFor(entry, text, cursorOffset))});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) {
                         if (a.rank != b.rank) return a.rank < b.rank;
                         return a.hint.qualifiedName < b.hint.qualifiedName;
                     });
    if (limit > 0 && ranked.size() > limit) ranked.resize(limit);
    std::vector<ScriptLanguageHint> out;
    out.reserve(ranked.size());
    for (Ranked& item : ranked) out.push_back(std::move(item.hint));
    return out;
}

std::vector<ScriptLanguageHint> scriptApiReferenceEntries() {
    std::vector<ScriptLanguageHint> out;
    out.reserve(Scripts::scriptApiCatalogV1().size());
    for (const Scripts::ScriptApiEntry& entry : Scripts::scriptApiCatalogV1()) {
        out.push_back(fromEntry(entry,
                                entry.insertText ? entry.insertText : entry.qualifiedName));
    }
    return out;
}

} // namespace ArtCade::EditorNative
