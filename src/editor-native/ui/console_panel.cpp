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

void ConsolePanel::updateCounts(Rml::ElementDocument* document,
                                const EditorCoordinator& coordinator) const {
    Rml::Element* counts = document->GetElementById("console-counts");
    if (!counts) return;
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const ConsoleMessage& message : coordinator.consoleLog()) {
        if (message.level == ConsoleMessage::Level::Error) ++errors;
        else if (message.level == ConsoleMessage::Level::Warning) ++warnings;
    }
    const auto piece = [](std::size_t n, const char* noun, const char* cls) {
        const std::string text = std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
        if (n == 0) return text;
        return "<span class=\"" + std::string(cls) + "\">" + text + "</span>";
    };
    counts->SetInnerRML(piece(errors, "error", "count-err") + " \xc2\xb7 "
                        + piece(warnings, "warning", "count-warn"));
}

void ConsolePanel::refresh(Rml::ElementDocument* document,
                           const EditorCoordinator& coordinator) {
    if (!document) return;
    updateCounts(document, coordinator);
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
