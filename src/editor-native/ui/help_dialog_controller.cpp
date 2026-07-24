#include "editor-native/ui/help_dialog_controller.h"

#include "editor-native/ui/ui_markup.h"

#include <RmlUi/Core/Context.h>

#include <sstream>

namespace ArtCade::EditorNative {

namespace {

std::string joinGestures(const std::vector<std::string>& gestures) {
    std::string out;
    for (std::size_t i = 0; i < gestures.size(); ++i) {
        if (i) out += "  /  ";
        out += gestures[i];
    }
    return out;
}

std::string filterTabClass(KeyboardShortcutsFilter filter,
                           KeyboardShortcutsFilter active) {
    return filter == active ? "shortcuts-tab active" : "shortcuts-tab";
}

const char* filterArg(KeyboardShortcutsFilter filter) {
    switch (filter) {
    case KeyboardShortcutsFilter::All: return "all";
    case KeyboardShortcutsFilter::General: return "general";
    case KeyboardShortcutsFilter::Scene: return "scene";
    case KeyboardShortcutsFilter::LogicBoard: return "logic";
    case KeyboardShortcutsFilter::ScriptEditor: return "script";
    }
    return "all";
}

} // namespace

HelpDialogController::HelpDialogController(Rml::ElementDocument* document)
    : document_(document) {}

void HelpDialogController::showHost() {
    if (!document_) return;
    if (Rml::Element* host = document_->GetElementById("editor-help-modal"))
        host->SetClass("hidden", false);
}

void HelpDialogController::hideHost() {
    if (!document_) return;
    if (Rml::Element* host = document_->GetElementById("editor-help-modal")) {
        host->SetInnerRML("");
        host->SetClass("hidden", true);
    }
}

void HelpDialogController::captureFocus() {
    previousFocus_ = nullptr;
    if (!document_ || !document_->GetContext()) return;
    previousFocus_ = document_->GetContext()->GetFocusElement();
}

void HelpDialogController::restoreFocus() {
    if (!document_ || !document_->GetContext()) {
        previousFocus_ = nullptr;
        return;
    }
    Rml::Context* context = document_->GetContext();
    if (previousFocus_ && previousFocus_->GetOwnerDocument() == document_) {
        previousFocus_->Focus();
    } else if (Rml::Element* root = document_->GetElementById("workspace")) {
        root->Focus();
    } else {
        context->GetFocusElement(); // no-op read
    }
    previousFocus_ = nullptr;
}

void HelpDialogController::focusInitialControl() {
    if (!document_) return;
    if (page_ == HelpDialogPage::KeyboardShortcuts) {
        if (Rml::Element* search = document_->GetElementById("help-shortcuts-search"))
            search->Focus();
    } else if (page_ == HelpDialogPage::About) {
        if (Rml::Element* close = document_->GetElementById("help-about-close"))
            close->Focus();
    }
}

std::vector<Rml::Element*> HelpDialogController::focusableElements() const {
    std::vector<Rml::Element*> out;
    if (!document_) return out;
    Rml::Element* host = document_->GetElementById("editor-help-modal");
    if (!host) return out;

    std::vector<Rml::Element*> stack{host};
    while (!stack.empty()) {
        Rml::Element* el = stack.back();
        stack.pop_back();
        if (!el) continue;
        const std::string tag = el->GetTagName();
        const bool focusableTag = tag == "input" || tag == "button" || tag == "textarea";
        const bool tabStop = el->HasAttribute("data-help-tabstop");
        if ((focusableTag || tabStop) && el->IsVisible())
            out.push_back(el);
        for (int i = static_cast<int>(el->GetNumChildren()) - 1; i >= 0; --i)
            stack.push_back(el->GetChild(static_cast<unsigned>(i)));
    }
    return out;
}

bool HelpDialogController::handleTabKey(bool shift) {
    if (!isOpen()) return false;
    auto focusables = focusableElements();
    if (focusables.empty()) return true;

    Rml::Element* current = document_ && document_->GetContext()
        ? document_->GetContext()->GetFocusElement()
        : nullptr;
    int index = -1;
    for (std::size_t i = 0; i < focusables.size(); ++i) {
        if (focusables[i] == current) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        focusables.front()->Focus();
        return true;
    }
    const int n = static_cast<int>(focusables.size());
    const int next = shift ? (index - 1 + n) % n : (index + 1) % n;
    focusables[static_cast<std::size_t>(next)]->Focus();
    return true;
}

void HelpDialogController::openKeyboardShortcuts(
    const KeyboardShortcutProjectionInput& input) {
    if (!document_) return;
    if (page_ == HelpDialogPage::KeyboardShortcuts) return;
    const bool wasOpen = isOpen();
    if (!wasOpen) captureFocus();
    page_ = HelpDialogPage::KeyboardShortcuts;
    filter_ = input.filter;
    search_ = std::string(input.search);
    availabilityContext_ = input.availabilityContext;
    renderKeyboardShortcuts();
    showHost();
    focusInitialControl();
}

void HelpDialogController::openAbout(const AboutArtCadeProjection& projection) {
    if (!document_) return;
    if (page_ == HelpDialogPage::About) return;
    const bool wasOpen = isOpen();
    if (!wasOpen) captureFocus();
    page_ = HelpDialogPage::About;
    about_ = projection;
    renderAbout();
    showHost();
    focusInitialControl();
}

void HelpDialogController::close() {
    if (!isOpen()) return;
    page_ = HelpDialogPage::None;
    search_.clear();
    filter_ = KeyboardShortcutsFilter::All;
    hideHost();
    restoreFocus();
}

void HelpDialogController::setShortcutFilter(KeyboardShortcutsFilter filter) {
    if (!isOpen() || page_ != HelpDialogPage::KeyboardShortcuts) return;
    filter_ = filter;
    renderKeyboardShortcuts(); // tabs active state needs a shell refresh
    focusInitialControl();
}

void HelpDialogController::setShortcutSearch(std::string search) {
    if (!isOpen() || page_ != HelpDialogPage::KeyboardShortcuts) return;
    search_ = std::move(search);
    renderShortcutsList();
}

void HelpDialogController::refreshKeyboardShortcuts(
    const KeyboardShortcutProjectionInput& input) {
    filter_ = input.filter;
    search_ = std::string(input.search);
    availabilityContext_ = input.availabilityContext;
    if (page_ == HelpDialogPage::KeyboardShortcuts)
        renderKeyboardShortcuts();
}

void HelpDialogController::renderShortcutsList() {
    if (!document_) return;
    Rml::Element* list = document_->GetElementById("help-shortcuts-list");
    if (!list) return;

    KeyboardShortcutProjectionInput input;
    input.filter = filter_;
    input.search = search_;
    input.availabilityContext = availabilityContext_;
    const auto items = buildKeyboardShortcutProjection(input);

    std::ostringstream html;
    std::string currentSection;
    for (const KeyboardShortcutListItem& item : items) {
        if (item.categoryLabel != currentSection) {
            currentSection = item.categoryLabel;
            html << "<div class=\"shortcut-section\">" << escapeRml(currentSection)
                 << "</div>";
        }
        html << "<div class=\"shortcut-row"
             << (item.currentlyAvailable ? "" : " shortcut-row-unavailable")
             << "\">"
                "<span class=\"shortcut-label\">" << escapeRml(item.label) << "</span>"
                "<span class=\"shortcut-gesture\">"
             << escapeRml(joinGestures(item.formattedGestures))
             << "</span></div>";
    }
    if (items.empty()) {
        html << "<div class=\"shortcuts-empty\">No matching shortcuts.</div>";
    }
    list->SetInnerRML(html.str());
}

void HelpDialogController::renderKeyboardShortcuts() {
    if (!document_) return;
    Rml::Element* host = document_->GetElementById("editor-help-modal");
    if (!host) return;

    std::ostringstream html;
    html << "<div class=\"help-dialog keyboard-shortcuts-dialog\" data-help-tabstop=\"1\">"
            "<div class=\"help-dialog-header\">"
            "<span class=\"help-dialog-title\">Keyboard Shortcuts</span>"
            "<button id=\"help-shortcuts-close\" class=\"help-dialog-close\" "
            "data-action=\"help-close\">×</button>"
            "</div>"
            "<div class=\"shortcuts-toolbar\">"
            "<input id=\"help-shortcuts-search\" class=\"shortcuts-search\" type=\"text\" "
            "data-action=\"help-shortcuts-search\" placeholder=\"Search actions…\" value=\""
         << escapeRml(search_)
         << "\"/>"
            "</div>"
            "<div class=\"shortcuts-tabs\">"
            "<button class=\""
         << filterTabClass(KeyboardShortcutsFilter::All, filter_)
         << "\" data-action=\"help-shortcuts-filter\" data-arg=\""
         << filterArg(KeyboardShortcutsFilter::All)
         << "\">All</button>"
            "<button class=\""
         << filterTabClass(KeyboardShortcutsFilter::General, filter_)
         << "\" data-action=\"help-shortcuts-filter\" data-arg=\""
         << filterArg(KeyboardShortcutsFilter::General)
         << "\">General</button>"
            "<button class=\""
         << filterTabClass(KeyboardShortcutsFilter::Scene, filter_)
         << "\" data-action=\"help-shortcuts-filter\" data-arg=\""
         << filterArg(KeyboardShortcutsFilter::Scene)
         << "\">Scene</button>"
            "<button class=\""
         << filterTabClass(KeyboardShortcutsFilter::LogicBoard, filter_)
         << "\" data-action=\"help-shortcuts-filter\" data-arg=\""
         << filterArg(KeyboardShortcutsFilter::LogicBoard)
         << "\">Logic Board</button>"
            "<button class=\""
         << filterTabClass(KeyboardShortcutsFilter::ScriptEditor, filter_)
         << "\" data-action=\"help-shortcuts-filter\" data-arg=\""
         << filterArg(KeyboardShortcutsFilter::ScriptEditor)
         << "\">Script</button>"
            "</div>"
            "<div id=\"help-shortcuts-list\" class=\"shortcuts-list\"></div>"
            "<div class=\"help-dialog-footer\">"
            "<button id=\"help-shortcuts-footer-close\" class=\"panel-btn\" "
            "data-action=\"help-close\">Close</button>"
            "</div></div>";

    host->SetInnerRML(html.str());
    renderShortcutsList();
}

void HelpDialogController::renderAbout() {
    if (!document_) return;
    Rml::Element* host = document_->GetElementById("editor-help-modal");
    if (!host) return;

    const EditorBuildInfo& editor = about_.editor;
    const BundledRuntimeInfo& runtime = about_.runtime;

    std::ostringstream html;
    html << "<div class=\"help-dialog about-dialog\" data-help-tabstop=\"1\">"
            "<div class=\"help-dialog-header\">"
            "<span class=\"help-dialog-title\">About ArtCade</span>"
            "<button id=\"help-about-x\" class=\"help-dialog-close\" "
            "data-action=\"help-close\">×</button>"
            "</div>"
            "<div class=\"about-body\">"
            "<div class=\"about-brand\">ArtCade</div>"
            "<div class=\"about-product\">" << escapeRml(editor.productName) << "</div>"
            "<div class=\"about-version\">Version " << escapeRml(editor.editorVersion)
         << "</div>"
            "<div class=\"about-tagline\">2D Game Engine and Visual Game Editor</div>"
            "<div class=\"about-meta\">"
            "<div class=\"about-meta-row\"><span class=\"about-meta-key\">Editor build</span>"
            "<span class=\"about-meta-value\">" << escapeRml(editor.editorBuildId)
         << "</span></div>"
            "<div class=\"about-meta-row\"><span class=\"about-meta-key\">Project format</span>"
            "<span class=\"about-meta-value\">" << editor.projectSchemaVersion
         << "</span></div>";

    if (runtime.available) {
        html << "<div class=\"about-meta-row\"><span class=\"about-meta-key\">Runtime version</span>"
                "<span class=\"about-meta-value\">" << escapeRml(runtime.engineVersion)
             << "</span></div>"
                "<div class=\"about-meta-row\"><span class=\"about-meta-key\">Runtime build</span>"
                "<span class=\"about-meta-value\">" << escapeRml(runtime.runtimeBuildId)
             << "</span></div>";
    } else {
        html << "<div class=\"about-meta-row\"><span class=\"about-meta-key\">Runtime template</span>"
                "<span class=\"about-meta-value\">Not installed</span></div>";
    }

    html << "<div class=\"about-meta-row about-copyright\"><span class=\"about-meta-value\">"
         << escapeRml(editor.copyright)
         << "</span></div>"
            "</div></div>"
            "<div class=\"help-dialog-footer\">"
            "<button id=\"help-about-close\" class=\"panel-btn\" data-action=\"help-close\">"
            "Close</button>"
            "</div></div>";

    host->SetInnerRML(html.str());
}

} // namespace ArtCade::EditorNative
