#include "editor-native/ui/console_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <algorithm>
#include <cctype>
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

bool levelShown(ConsoleMessage::Level level, const EditorUiState& uiState) {
    switch (level) {
        case ConsoleMessage::Level::Error:   return uiState.consoleShowError;
        case ConsoleMessage::Level::Warning: return uiState.consoleShowWarning;
        default:                             return uiState.consoleShowInfo;
    }
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matchesFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    return lower(text).find(lower(filter)) != std::string::npos;
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
    if (counts) {
        counts->SetInnerRML(piece(errors, "error", "count-err") + " \xc2\xb7 "
                            + piece(warnings, "warning", "count-warn"));
    }
    if (Rml::Element* health = document->GetElementById("status-health")) {
        health->SetClass("ready", errors == 0 && warnings == 0);
        health->SetClass("warning", errors == 0 && warnings != 0);
        health->SetClass("error", errors != 0);
        std::string label = "<span class=\"status-dot\"></span>";
        if (errors != 0) {
            label += std::to_string(errors) + (errors == 1 ? " error" : " errors");
        } else if (warnings != 0) {
            label += std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
        } else {
            label += "Ready";
        }
        health->SetInnerRML(label);
    }
}

void ConsolePanel::updateToolbar(Rml::ElementDocument* document,
                                 const EditorCoordinator& coordinator) const {
    const EditorUiState& uiState = coordinator.uiState();
    const auto setToggle = [&](const char* id, bool active) {
        if (Rml::Element* el = document->GetElementById(id)) el->SetClass("active", active);
    };
    setToggle("btn-console-info",    uiState.consoleShowInfo);
    setToggle("btn-console-warning", uiState.consoleShowWarning);
    setToggle("btn-console-error",   uiState.consoleShowError);

    // Text inputs parsed statically from RML do not render in this RmlUi
    // build (every working field elsewhere is injected via SetInnerRML), so
    // the search field is built once into its slot on first refresh.
    if (Rml::Element* slot = document->GetElementById("console-search-slot")) {
        if (!slot->HasChildNodes()) {
            slot->SetInnerRML(
                "<input id=\"console-filter-input\" type=\"text\""
                " class=\"console-search-field\" data-action=\"commit-console-filter\"/>");
        }
    }

    // The typed buffer is the freshest copy of what the user is entering; only
    // resync the field's value from uiState when it does not currently hold
    // focus, so a new log line arriving mid-keystroke can't clobber it.
    Rml::Element* input = document->GetElementById("console-filter-input");
    Rml::Context* context = document->GetContext();
    if (input && (!context || context->GetFocusElement() != input)) {
        input->SetAttribute("value", uiState.consoleFilter);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input))
            control->SetValue(uiState.consoleFilter);
    }
}

void ConsolePanel::refresh(Rml::ElementDocument* document,
                           const EditorCoordinator& coordinator) {
    if (!document) return;
    updateCounts(document, coordinator);
    updateToolbar(document, coordinator);
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

    const EditorUiState& uiState = coordinator.uiState();
    std::string html;
    std::size_t shown = 0;
    constexpr std::size_t maxShown = 200;
    // Walk from the newest message backward so the level/search filters keep
    // the most recent 200 *matching* lines, not the most recent 200 overall.
    for (std::size_t i = messages.size(); i-- > 0 && shown < maxShown; ) {
        const ConsoleMessage& message = messages[i];
        if (!levelShown(message.level, uiState)) continue;
        if (!matchesFilter(message.text, uiState.consoleFilter)) continue;
        std::string row = "<div class=\"log ";
        row += levelClass(message.level);
        if (selected_ && *selected_ == i) row += " selected";
        row += "\" data-action=\"select-console\" data-arg=\"" + std::to_string(i) + "\">";
        row += escapeRml(message.text) + "</div>";
        html = row + html;   // prepend: keep chronological order in the list
        ++shown;
    }
    if (shown == 0) {
        html = "<div class=\"log log-info\">No messages match the current filter.</div>";
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
