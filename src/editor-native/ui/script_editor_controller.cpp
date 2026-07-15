#include "editor-native/ui/script_editor_controller.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/model/script_editor_state.h"
#include "editor-native/ui/ui_markup.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

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
    surface_.reset();
    document_ = nullptr;
}

void ScriptEditorController::refresh() {
    if (!document_) return;
    refreshTabs();
    syncSurfaceFromActiveBuffer();
    refreshLineNumbers();
    refreshStatus();
    const bool hasBuffer = coordinator_.state().scriptEditor.active() != nullptr;
    if (Rml::Element* empty = document_->GetElementById("script-editor-empty"))
        empty->SetClass("hidden", hasBuffer);
    if (Rml::Element* body = document_->GetElementById("script-editor-body"))
        body->SetClass("hidden", !hasBuffer);
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
}

void ScriptEditorController::refreshLineNumbers() {
    Rml::Element* lines = document_->GetElementById("script-line-numbers");
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!lines || !buffer) return;
    std::size_t count = 1;
    for (char c : buffer->text) if (c == '\n') ++count;
    std::string rml;
    for (std::size_t line = 1; line <= count; ++line)
        rml += std::to_string(line) + (line == count ? "" : "<br/>");
    lines->SetInnerRML(rml);
    if (surface_) lines->SetScrollTop(surface_->scrollTop());
}

void ScriptEditorController::refreshStatus() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    const ScriptCursorPosition cursor = buffer
        ? scriptCursorPosition(buffer->text, buffer->cursorOffset)
        : ScriptCursorPosition{};
    if (Rml::Element* status = document_->GetElementById("script-cursor-status")) {
        status->SetInnerRML("Lua  &#xb7;  Ln " + std::to_string(cursor.line)
                            + ", Col " + std::to_string(cursor.column)
                            + "  &#xb7;  Spaces: 4");
    }
}

void ScriptEditorController::textChanged(const std::string& text) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
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
    refreshStatus();
}

void ScriptEditorController::cursorChanged() {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active();
    if (!buffer || !surface_) return;
    coordinator_.apply(SetScriptCursorIntent{
        buffer->scriptAssetId, surface_->cursorOffset(), surface_->scrollTop()});
    if (document_) {
        if (Rml::Element* lines = document_->GetElementById("script-line-numbers"))
            lines->SetScrollTop(surface_->scrollTop());
    }
    refreshStatus();
}

void ScriptEditorController::setFocused(bool focused) {
    coordinator_.apply(SetScriptEditorFocusIntent{focused});
    if (!focused) cursorChanged();
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
    std::string next = surface_->text();
    const std::size_t begin = std::min(selectionBegin, next.size());
    const std::size_t end = std::min(std::max(selectionEnd, begin), next.size());
    next.replace(begin, end - begin, 4, ' ');
    surface_->setText(next, begin + 4, surface_->scrollTop());
    textChanged(next);
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

} // namespace ArtCade::EditorNative
