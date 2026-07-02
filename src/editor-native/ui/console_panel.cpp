#include "editor-native/ui/console_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string>

namespace ArtCade::EditorNative {

namespace {

const char* levelClass(ConsoleMessage::Level level) {
    switch (level) {
        case ConsoleMessage::Level::Error:   return "log-error";
        case ConsoleMessage::Level::Warning: return "log-warning";
        default:                             return "log-info";
    }
}

} // namespace

void ConsolePanel::updateCopyButton(Rml::ElementDocument* document) const {
    if (!document) return;
    if (Rml::Element* copy = document->GetElementById("btn-console-copy"))
        copy->SetClass("disabled", !selected_.has_value());
}

void ConsolePanel::refresh(Rml::ElementDocument* document,
                           const EditorCoordinator& coordinator) {
    if (!document) return;
    Rml::Element* list = document->GetElementById("console-list");
    if (!list) return;

    const auto& messages = coordinator.consoleLog();
    // Clamp the selection to the live log before drawing: a shrunk log (or a
    // replaced project) must not leave a dangling highlight or a copyable ghost.
    if (selected_ && *selected_ >= messages.size()) selected_.reset();

    if (messages.empty()) {
        list->SetInnerRML("<div class=\"log log-info\">Console ready.</div>");
        updateCopyButton(document);
        return;
    }

    std::string html;
    const std::size_t maxShown = 200;
    const std::size_t start = messages.size() > maxShown ? messages.size() - maxShown : 0;
    for (std::size_t i = start; i < messages.size(); ++i) {
        html += "<div class=\"log ";
        html += levelClass(messages[i].level);
        if (selected_ && *selected_ == i) html += " selected";
        html += "\" data-action=\"select-console\" data-arg=\"" + std::to_string(i) + "\">";
        html += escapeRml(messages[i].text) + "</div>";
    }
    list->SetInnerRML(html);
    updateCopyButton(document);
}

void ConsolePanel::select(std::size_t messageIndex, Rml::ElementDocument* document,
                          const EditorCoordinator& coordinator) {
    if (messageIndex >= coordinator.consoleLog().size()) return;
    selected_ = messageIndex;
    refresh(document, coordinator);   // repaint highlight + Copy button
}

} // namespace ArtCade::EditorNative
