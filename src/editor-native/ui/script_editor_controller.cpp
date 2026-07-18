#include "editor-native/ui/script_editor_controller.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/script_syntax_validator.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/model/script_editor_state.h"
#include "editor-native/model/script_language_service.h"
#include "editor-native/model/script_text_ops.h"
#include "editor-native/ui/ui_markup.h"

#include "script-api-catalog.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

class RmlTextareaCodeEditorSurface final : public ICodeEditorSurface {
public:
    explicit RmlTextareaCodeEditorSurface(Rml::ElementDocument* document)
        : document_(document) {}

    void setText(const std::string& text, std::size_t cursorOffset,
                 float scrollTop) override {
        if (auto* area = textarea()) {
            if (area->GetValue() != text) area->SetValue(text);
            const int byteCursor = static_cast<int>(std::min<std::size_t>(
                cursorOffset, static_cast<std::size_t>(std::numeric_limits<int>::max())));
            const int cursor = Rml::StringUtilities::ConvertByteOffsetToCharacterOffset(
                text, byteCursor);
            area->SetSelectionRange(cursor, cursor);
            area->SetScrollTop(scrollTop);
        }
    }
    std::string text() const override {
        const auto* area = textarea();
        return area ? area->GetValue() : std::string{};
    }
    std::size_t cursorOffset() const override {
        int begin = 0, end = 0;
        const auto* area = textarea();
        if (area) area->GetSelection(&begin, &end, nullptr);
        const std::string value = area ? area->GetValue() : std::string{};
        return static_cast<std::size_t>(std::max(0,
            Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(value, end)));
    }
    std::pair<std::size_t, std::size_t> selection() const override {
        int begin = 0, end = 0;
        const auto* area = textarea();
        if (!area) return {};
        area->GetSelection(&begin, &end, nullptr);
        const std::string value = area->GetValue();
        const auto toByte = [&](int characterOffset) {
            return static_cast<std::size_t>(std::max(0,
                Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(
                    value, characterOffset)));
        };
        return {toByte(begin), toByte(end)};
    }
    float scrollTop() const override {
        const auto* area = textarea();
        return area ? const_cast<Rml::ElementFormControlTextArea*>(area)->GetScrollTop() : 0.f;
    }
    float scrollLeft() const override {
        const auto* area = textarea();
        return area ? const_cast<Rml::ElementFormControlTextArea*>(area)->GetScrollLeft() : 0.f;
    }
    void setSelection(std::size_t begin, std::size_t end) override {
        if (auto* area = textarea()) {
            const std::string value = area->GetValue();
            const auto toCharacter = [&](std::size_t byteOffset) {
                const int clamped = static_cast<int>(std::min<std::size_t>(
                    byteOffset, static_cast<std::size_t>(std::numeric_limits<int>::max())));
                return Rml::StringUtilities::ConvertByteOffsetToCharacterOffset(value, clamped);
            };
            area->SetSelectionRange(toCharacter(begin), toCharacter(end));
        }
    }
    void focus() override { if (auto* area = textarea()) area->Focus(true); }

private:
    Rml::ElementFormControlTextArea* textarea() {
        return document_ ? rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(
            document_->GetElementById("script-editor-textarea")) : nullptr;
    }
    const Rml::ElementFormControlTextArea* textarea() const {
        return document_ ? rmlui_dynamic_cast<const Rml::ElementFormControlTextArea*>(
            document_->GetElementById("script-editor-textarea")) : nullptr;
    }
    Rml::ElementDocument* document_ = nullptr;
};

std::string scriptName(const EditorCoordinator& coordinator, const AssetId& assetId) {
    if (const ScriptAssetDef* asset = coordinator.document().findScriptAsset(assetId)) {
        return asset->name.empty() ? asset->assetId : asset->name;
    }
    return assetId;
}

} // namespace

ScriptEditorController::ScriptEditorController(EditorCoordinator& coordinator,
                                               Rml::ElementDocument* document)
    : coordinator_(coordinator), document_(document),
      surface_(std::make_unique<RmlTextareaCodeEditorSurface>(document)) {}

void ScriptEditorController::detach() {
    pendingValidation_.clear();
    surface_.reset();
    document_ = nullptr;
}

void ScriptEditorController::refresh() {
    if (!document_) return;
    refreshTabs();
    syncSurfaceFromActiveBuffer();
    refreshLineNumbers();
    refreshHighlight();
    refreshCaretAndCurrentLine();
    refreshApiPanel();
    refreshLanguageHint();
    refreshStatus();
    refreshDiagnostics();
    refreshApplyBanner();
    const bool hasBuffer = coordinator_.state().scriptEditor.active() != nullptr;
    if (Rml::Element* empty = document_->GetElementById("script-editor-empty"))
        empty->SetClass("hidden", hasBuffer);
    if (Rml::Element* body = document_->GetElementById("script-editor-body"))
        body->SetClass("hidden", !hasBuffer);
}

void ScriptEditorController::processFrame() {
    using namespace std::chrono_literals;
    const auto now = std::chrono::steady_clock::now();
    const ScriptEditorState& state = coordinator_.state().scriptEditor;
    for (auto it = pendingValidation_.begin(); it != pendingValidation_.end(); ) {
        if (!state.find(it->first)) it = pendingValidation_.erase(it);
        else ++it;
    }
    for (const ScriptEditorBuffer& buffer : state.buffers) {
        if (!buffer.validationPending) {
            pendingValidation_.erase(buffer.scriptAssetId);
            continue;
        }
        auto [it, inserted] = pendingValidation_.try_emplace(
            buffer.scriptAssetId,
            PendingValidation{buffer.revision, now + 400ms});
        if (!inserted && it->second.revision != buffer.revision)
            it->second = PendingValidation{buffer.revision, now + 400ms};
    }

    std::vector<std::pair<AssetId, std::uint64_t>> ready;
    for (const auto& [assetId, pending] : pendingValidation_) {
        if (pending.due <= now) ready.emplace_back(assetId, pending.revision);
    }
    for (const auto& [assetId, revision] : ready) {
        pendingValidation_.erase(assetId);
        validate(assetId, revision);
    }

    if (document_ && surface_ && state.editorFocused) {
        const float top = surface_->scrollTop();
        const float left = surface_->scrollLeft();
        if (top != lastSyncedScrollTop_ || left != lastSyncedScrollLeft_) {
            lastSyncedScrollTop_ = top;
            lastSyncedScrollLeft_ = left;
            syncOverlayScroll();
            refreshCaretAndCurrentLine();
        }
    }
}

void ScriptEditorController::validateActive() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer) return;
    pendingValidation_.erase(buffer->scriptAssetId);
    validate(buffer->scriptAssetId, buffer->revision);
    if (const ScriptEditorBuffer* validated =
            coordinator_.state().scriptEditor.find(buffer->scriptAssetId)) {
        if (validated->diagnostics.empty())
            coordinator_.logInfo("No Lua syntax errors in "
                                 + scriptName(coordinator_, validated->scriptAssetId));
    }
}

void ScriptEditorController::validate(const AssetId& assetId, std::uint64_t revision) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.find(assetId);
    const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
    if (!buffer || !asset || buffer->revision != revision) return;
    coordinator_.apply(SetScriptDiagnosticsIntent{
        assetId, revision,
        validateScriptSyntax(assetId, asset->sourcePath, buffer->text)});
}

void ScriptEditorController::refreshTabs() {
    Rml::Element* tabs = document_->GetElementById("script-editor-tabs");
    if (!tabs) return;
    std::string rml;
    const ScriptEditorState& state = coordinator_.state().scriptEditor;
    for (const ScriptEditorBuffer& buffer : state.buffers) {
        const bool active = state.activeAssetId == buffer.scriptAssetId;
        rml += "<div class=\"script-tab";
        if (active) rml += " active";
        rml += "\" data-action=\"activate-script\" data-arg=\""
            + escapeRml(buffer.scriptAssetId) + "\"><span class=\"script-tab-name\">"
            + escapeRml(scriptName(coordinator_, buffer.scriptAssetId)) + "</span>";
        if (buffer.dirty()) rml += "<span class=\"script-tab-dirty\">*</span>";
        rml += "<button class=\"script-tab-close\" data-action=\"close-script\" data-arg=\""
            + escapeRml(buffer.scriptAssetId) + "\">&#xd7;</button></div>";
    }
    tabs->SetInnerRML(rml);
}

void ScriptEditorController::syncSurfaceFromActiveBuffer() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) {
        renderedAssetId_.clear();
        renderedRevision_ = 0;
        return;
    }
    if (renderedAssetId_ == buffer->scriptAssetId
        && renderedRevision_ == buffer->revision) return;
    surface_->setText(buffer->text, buffer->cursorOffset, buffer->scrollTop);
    renderedAssetId_ = buffer->scriptAssetId;
    renderedRevision_ = buffer->revision;
    refreshHighlight();
}

void ScriptEditorController::refreshLineNumbers() {
    Rml::Element* lines = document_->GetElementById("script-line-numbers");
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!lines || !buffer) return;
    std::size_t count = 1;
    for (char c : buffer->text) if (c == '\n') ++count;
    std::unordered_set<int> errorLines;
    for (const ScriptDiagnostic& diagnostic : buffer->diagnostics) {
        if (diagnostic.line > 0) errorLines.insert(diagnostic.line);
    }
    const ScriptCursorPosition cursor =
        scriptCursorPosition(buffer->text, buffer->cursorOffset);
    std::string rml;
    for (std::size_t line = 1; line <= count; ++line) {
        const bool isError = errorLines.count(static_cast<int>(line)) != 0;
        const bool isCurrent = line == cursor.line;
        if (isError || isCurrent) {
            rml += "<span class=\"";
            if (isCurrent) rml += "script-line-current";
            if (isError) {
                if (isCurrent) rml += " ";
                rml += "script-line-error";
            }
            rml += "\">" + std::to_string(line) + "</span>";
        } else {
            rml += std::to_string(line);
        }
        if (line != count) rml += "<br/>";
    }
    lines->SetInnerRML(rml);
    if (surface_) lines->SetScrollTop(surface_->scrollTop());
}

void ScriptEditorController::refreshHighlight() {
    Rml::Element* highlight = document_ ? document_->GetElementById("script-editor-highlight")
                                        : nullptr;
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!highlight) return;
    if (!buffer) {
        highlight->SetInnerRML({});
        return;
    }
    ScriptHighlightDecorations decorations;
    for (const ScriptDiagnostic& diagnostic : buffer->diagnostics) {
        if (diagnostic.line > 0) decorations.errorLines.push_back(diagnostic.line);
    }
    decorations.bracketMatch = bracketDecoration_;
    highlight->SetInnerRML(highlightLuaToRml(buffer->text, decorations));
    syncOverlayScroll();
}

void ScriptEditorController::syncOverlayScroll() {
    if (!document_ || !surface_) return;
    const float top = surface_->scrollTop();
    const float left = surface_->scrollLeft();
    if (Rml::Element* lines = document_->GetElementById("script-line-numbers"))
        lines->SetScrollTop(top);
    if (Rml::Element* highlight = document_->GetElementById("script-editor-highlight")) {
        highlight->SetScrollTop(top);
        highlight->SetScrollLeft(left);
    }
}

void ScriptEditorController::refreshCaretAndCurrentLine() {
    if (!document_ || !surface_) return;
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    Rml::Element* caret = document_->GetElementById("script-editor-caret");
    Rml::Element* band = document_->GetElementById("script-editor-current-line");
    if (!buffer) {
        if (caret) caret->SetClass("visible", false);
        if (band) band->SetClass("visible", false);
        return;
    }
    // Metrics must match script_editor.rcss (padding 12dp, line-height 18dp,
    // JetBrains Mono 12dp ≈ 0.6em advance).
    constexpr float kPad = 12.f;
    constexpr float kLineHeight = 18.f;
    constexpr float kCharWidth = 7.2f;
    const ScriptCursorPosition cursor =
        scriptCursorPosition(buffer->text, surface_->cursorOffset());
    const float top = kPad - surface_->scrollTop()
        + static_cast<float>(cursor.line - 1) * kLineHeight;
    const float left = kPad - surface_->scrollLeft()
        + static_cast<float>(cursor.column - 1) * kCharWidth;
    if (band) {
        band->SetProperty("top", std::to_string(top) + "dp");
        band->SetClass("visible", true);
    }
    if (caret) {
        caret->SetProperty("top", std::to_string(top) + "dp");
        caret->SetProperty("left", std::to_string(left) + "dp");
        caret->SetClass("visible", coordinator_.state().scriptEditor.editorFocused);
    }
}

void ScriptEditorController::applyTextEdit(const ScriptTextEditResult& edit) {
    if (!surface_) return;
    bracketDecoration_.reset();
    surface_->setText(edit.text, edit.selectionEnd, surface_->scrollTop());
    surface_->setSelection(edit.selectionBegin, edit.selectionEnd);
    textChanged(edit.text);
    refreshHighlight();
    refreshCaretAndCurrentLine();
}

void ScriptEditorController::refreshStatus() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    const ScriptCursorPosition cursor = buffer
        ? scriptCursorPosition(buffer->text, buffer->cursorOffset)
        : ScriptCursorPosition{};
    if (Rml::Element* status = document_->GetElementById("script-cursor-status")) {
        std::string validation;
        if (buffer && buffer->validationPending) validation = "  &#xb7;  Checking...";
        else if (buffer && buffer->diagnostics.empty()) validation = "  &#xb7;  No problems";
        else if (buffer) validation = "  &#xb7;  "
            + std::to_string(buffer->diagnostics.size())
            + (buffer->diagnostics.size() == 1 ? " problem" : " problems");
        status->SetInnerRML("Lua 5.4  &#xb7;  Ln " + std::to_string(cursor.line)
                            + ", Col " + std::to_string(cursor.column)
                            + "  &#xb7;  Spaces: 4" + validation);
    }
}

void ScriptEditorController::refreshDiagnostics() {
    Rml::Element* list = document_ ? document_->GetElementById("script-diagnostics") : nullptr;
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!list) return;
    if (!buffer || buffer->validationPending || buffer->diagnostics.empty()) {
        list->SetInnerRML({});
        list->SetClass("hidden", true);
        refreshHighlight();
        refreshLineNumbers();
        return;
    }
    std::string rml;
    for (const ScriptDiagnostic& diagnostic : buffer->diagnostics) {
        rml += "<button class=\"script-diagnostic\" data-action=\"goto-script-line\" data-arg=\""
            + std::to_string(std::max(1, diagnostic.line)) + "\">"
            + escapeRml(diagnostic.path) + ":" + std::to_string(diagnostic.line)
            + ":" + std::to_string(diagnostic.column) + "  "
            + escapeRml(diagnostic.message) + "</button>";
    }
    list->SetInnerRML(rml);
    list->SetClass("hidden", false);
    refreshHighlight();
    refreshLineNumbers();
}

void ScriptEditorController::refreshApplyBanner() {
    Rml::Element* banner = document_
        ? document_->GetElementById("script-restart-banner") : nullptr;
    if (!banner) return;
    banner->SetClass("hidden", !coordinator_.scriptRestartRequired());
}

void ScriptEditorController::textChanged(const std::string& text) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    bracketDecoration_.reset();
    const bool wasDirty = buffer->dirty();
    coordinator_.apply(EditScriptBufferIntent{
        buffer->scriptAssetId, text, surface_->cursorOffset()});
    renderedAssetId_ = buffer->scriptAssetId;
    renderedRevision_ = buffer->revision;
    // Dirty decoration changes only on the clean<->dirty edge. Rebuilding the
    // lightweight tab strip once at that edge keeps the textarea and the panel
    // itself stable during continued typing.
    if (wasDirty != buffer->dirty()) refreshTabs();
    refreshLineNumbers();
    refreshHighlight();
    refreshCaretAndCurrentLine();
    refreshStatus();
}

void ScriptEditorController::cursorChanged() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    coordinator_.apply(SetScriptCursorIntent{
        buffer->scriptAssetId, surface_->cursorOffset(), surface_->scrollTop()});
    lastSyncedScrollTop_ = surface_->scrollTop();
    lastSyncedScrollLeft_ = surface_->scrollLeft();
    syncOverlayScroll();
    refreshLineNumbers();
    refreshCaretAndCurrentLine();
    refreshLanguageHint();
    refreshStatus();
}

void ScriptEditorController::setFocused(bool focused) {
    coordinator_.apply(SetScriptEditorFocusIntent{focused});
    if (!focused) cursorChanged();
    else refreshCaretAndCurrentLine();
}

void ScriptEditorController::undo() {
    cursorChanged();
    coordinator_.apply(UndoScriptBufferIntent{});
}
void ScriptEditorController::redo() {
    cursorChanged();
    coordinator_.apply(RedoScriptBufferIntent{});
}

void ScriptEditorController::insertSpacesForTab() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto [selectionBegin, selectionEnd] = surface_->selection();
    applyTextEdit(indentOrInsertTab(surface_->text(), selectionBegin, selectionEnd));
}

void ScriptEditorController::outdent() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto [selectionBegin, selectionEnd] = surface_->selection();
    applyTextEdit(outdentSelection(surface_->text(), selectionBegin, selectionEnd));
}

void ScriptEditorController::toggleComment() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto [selectionBegin, selectionEnd] = surface_->selection();
    applyTextEdit(toggleLineComment(surface_->text(), selectionBegin, selectionEnd));
}

void ScriptEditorController::autoIndentNewline() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    applyTextEdit(insertNewlineWithAutoIndent(surface_->text(), surface_->cursorOffset()));
}

void ScriptEditorController::jumpToMatchingBracket() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto match = findMatchingBracket(surface_->text(), surface_->cursorOffset());
    if (!match) return;
    bracketDecoration_ = match;
    surface_->setSelection(match->first, match->second + 1);
    surface_->focus();
    refreshHighlight();
    cursorChanged();
}

void ScriptEditorController::duplicateLines() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto [selectionBegin, selectionEnd] = surface_->selection();
    applyTextEdit(duplicateSelectionLines(surface_->text(), selectionBegin, selectionEnd));
}

void ScriptEditorController::moveLines(int direction) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    const auto [selectionBegin, selectionEnd] = surface_->selection();
    applyTextEdit(moveSelectionLines(surface_->text(), selectionBegin, selectionEnd, direction));
}

void ScriptEditorController::refreshApiPanel() {
    Rml::Element* list = document_ ? document_->GetElementById("script-api-list") : nullptr;
    if (!list) return;
    std::string rml;
    for (const ScriptLanguageHint& entry : scriptApiReferenceEntries()) {
        rml += "<button class=\"script-api-item\" data-action=\"insert-script-api\" data-arg=\""
            + escapeRml(entry.qualifiedName) + "\" title=\""
            + escapeRml(entry.detail) + "\">"
            + escapeRml(entry.qualifiedName) + "</button>";
    }
    list->SetInnerRML(rml);
}

void ScriptEditorController::refreshLanguageHint() {
    Rml::Element* hint = document_ ? document_->GetElementById("script-api-hint") : nullptr;
    if (!hint || !surface_) return;
    const ScriptLanguageHint signature = scriptSignatureAt(surface_->text(), surface_->cursorOffset());
    if (!signature.title.empty()) {
        hint->SetInnerRML(escapeRml(signature.title));
        return;
    }
    const ScriptLanguageHint hover = scriptHoverAt(surface_->text(), surface_->cursorOffset());
    hint->SetInnerRML(hover.title.empty() ? std::string{} : escapeRml(hover.title));
}

void ScriptEditorController::showCompletions() {
    if (!document_ || !surface_) return;
    Rml::Element* popup = document_->GetElementById("script-completion-popup");
    if (!popup) return;
    const auto completions = scriptCompletionsAt(surface_->text(), surface_->cursorOffset());
    completionInserts_.clear();
    if (completions.empty()) {
        popup->SetInnerRML({});
        popup->SetClass("hidden", true);
        return;
    }
    std::string rml;
    for (std::size_t i = 0; i < completions.size(); ++i) {
        completionInserts_.push_back(completions[i].insertText);
        rml += "<button class=\"script-completion-item";
        if (i == 0) rml += " active";
        rml += "\" data-action=\"accept-script-completion\" data-arg=\""
            + std::to_string(i) + "\">"
            + escapeRml(completions[i].qualifiedName) + "  "
            + escapeRml(completions[i].title) + "</button>";
    }
    popup->SetInnerRML(rml);
    popup->SetClass("hidden", false);
}

void ScriptEditorController::hideCompletions() {
    completionInserts_.clear();
    if (!document_) return;
    if (Rml::Element* popup = document_->GetElementById("script-completion-popup")) {
        popup->SetInnerRML({});
        popup->SetClass("hidden", true);
    }
}

void ScriptEditorController::acceptCompletion(const std::string& insertText) {
    if (!surface_ || insertText.empty()) return;
    hideCompletions();
    const ScriptCompletionEdit edit =
        applyScriptCompletionInsert(surface_->text(), surface_->cursorOffset(), insertText);
    applyTextEdit(ScriptTextEditResult{
        edit.text, edit.selectionBegin, edit.selectionEnd});
}

void ScriptEditorController::acceptCompletionAt(std::size_t index) {
    if (index >= completionInserts_.size()) return;
    acceptCompletion(completionInserts_[index]);
}

void ScriptEditorController::insertApiSnippet(const std::string& qualifiedName) {
    const Scripts::ScriptApiEntry* entry =
        Scripts::findScriptApiByQualifiedName(qualifiedName);
    if (!entry || !surface_) return;
    acceptCompletion(scriptInsertTextFor(*entry, surface_->text(), surface_->cursorOffset()));
    surface_->focus();
}

void ScriptEditorController::findNext(const std::string& query) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_ || query.empty()) return;
    coordinator_.apply(SetScriptSearchIntent{query});
    const std::size_t start = std::min(surface_->cursorOffset(), buffer->text.size());
    std::size_t found = buffer->text.find(query, start);
    if (found == std::string::npos) found = buffer->text.find(query);
    if (found != std::string::npos) {
        surface_->setSelection(found, found + query.size());
        surface_->focus();
        cursorChanged();
    }
}

void ScriptEditorController::goToLine(const std::string& lineText) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    char* end = nullptr;
    const unsigned long requested = std::strtoul(lineText.c_str(), &end, 10);
    if (!end || *end != '\0' || requested == 0) return;
    std::size_t offset = 0, line = 1;
    while (line < requested && offset < buffer->text.size()) {
        const std::size_t next = buffer->text.find('\n', offset);
        if (next == std::string::npos) { offset = buffer->text.size(); break; }
        offset = next + 1;
        ++line;
    }
    surface_->setSelection(offset, offset);
    surface_->focus();
    cursorChanged();
}

void ScriptEditorController::goToLocation(int requestedLine, int requestedColumn) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_ || requestedLine <= 0) return;
    std::size_t offset = 0;
    int line = 1;
    while (line < requestedLine && offset < buffer->text.size()) {
        const std::size_t next = buffer->text.find('\n', offset);
        if (next == std::string::npos) { offset = buffer->text.size(); break; }
        offset = next + 1;
        ++line;
    }
    int column = 1;
    while (column < std::max(1, requestedColumn) && offset < buffer->text.size()
           && buffer->text[offset] != '\n') {
        ++offset;
        while (offset < buffer->text.size()
               && (static_cast<unsigned char>(buffer->text[offset]) & 0xc0u) == 0x80u) {
            ++offset;
        }
        ++column;
    }
    std::size_t highlightEnd = offset;
    if (highlightEnd < buffer->text.size() && buffer->text[highlightEnd] != '\n') {
        ++highlightEnd;
        while (highlightEnd < buffer->text.size()
               && (static_cast<unsigned char>(buffer->text[highlightEnd]) & 0xc0u) == 0x80u) {
            ++highlightEnd;
        }
    }
    surface_->setSelection(offset, highlightEnd);
    surface_->focus();
    cursorChanged();
}

} // namespace ArtCade::EditorNative
