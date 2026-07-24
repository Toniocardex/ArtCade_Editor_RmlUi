#include "editor-native/app/keyboard_focus_tracker.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

namespace ArtCade::EditorNative {

namespace {

KeyboardFocusDomain domainFromAttribute(const Rml::String& value) {
    if (value == "scene-viewport") return KeyboardFocusDomain::SceneViewport;
    if (value == "hierarchy") return KeyboardFocusDomain::Hierarchy;
    if (value == "assets") return KeyboardFocusDomain::Assets;
    if (value == "inspector") return KeyboardFocusDomain::Inspector;
    if (value == "tile-palette") return KeyboardFocusDomain::TilePalette;
    if (value == "logic-board") return KeyboardFocusDomain::LogicBoard;
    if (value == "script-text") return KeyboardFocusDomain::ScriptTextEditor;
    if (value == "console") return KeyboardFocusDomain::Console;
    if (value == "text-control") return KeyboardFocusDomain::TextControl;
    return KeyboardFocusDomain::None;
}

KeyboardFocusDomain inferFromElement(Rml::Element* el) {
    for (; el; el = el->GetParentNode()) {
        const Rml::String domainAttr =
            el->GetAttribute("data-keyboard-domain", Rml::String());
        if (!domainAttr.empty()) {
            const auto d = domainFromAttribute(domainAttr);
            if (d != KeyboardFocusDomain::None) return d;
        }
        const Rml::String id = el->GetId();
        if (id == "console" || id == "console-list") return KeyboardFocusDomain::Console;
        if (id == "hierarchy-pane" || id == "hierarchy-list" || id == "hierarchy"
            || id == "left-col")
            return KeyboardFocusDomain::Hierarchy;
        if (id == "assets-panel" || id == "assets") return KeyboardFocusDomain::Assets;
        if (id == "inspector" || id == "right-col") return KeyboardFocusDomain::Inspector;
        if (id == "tile-palette" || id == "tile-palette-dock"
            || id == "tile-palette-dock-body")
            return KeyboardFocusDomain::TilePalette;
        if (id == "viewport" || id == "center-content")
            return KeyboardFocusDomain::SceneViewport;
        if (id == "logic-board" || id == "logic-workspace")
            return KeyboardFocusDomain::LogicBoard;
        if (id == "script-editor" || id == "script-workspace")
            return KeyboardFocusDomain::ScriptTextEditor;
    }
    return KeyboardFocusDomain::None;
}

} // namespace

void KeyboardFocusTracker::clear() { domain_ = KeyboardFocusDomain::None; }

void KeyboardFocusTracker::claim(KeyboardFocusDomain domain) { domain_ = domain; }

void KeyboardFocusTracker::claimFromElement(Rml::Element* element) {
    if (const auto inferred = inferFromElement(element);
        inferred != KeyboardFocusDomain::None)
        domain_ = inferred;
}

void KeyboardFocusTracker::sync(Rml::Context* context,
                                bool windowFocused,
                                bool modalOpen,
                                bool scriptEditorFocused,
                                bool textFieldFocused,
                                EditorWorkspaceKind workspace,
                                bool workspaceChanged) {
    if (!windowFocused || modalOpen) {
        clear();
        workspace_ = workspace;
        return;
    }
    if (workspaceChanged || workspace != workspace_) {
        clear();
        workspace_ = workspace;
    }

    if (scriptEditorFocused) {
        domain_ = KeyboardFocusDomain::ScriptTextEditor;
        return;
    }
    if (textFieldFocused) {
        if (context) {
            if (const auto inferred = inferFromElement(context->GetFocusElement());
                inferred != KeyboardFocusDomain::None) {
                domain_ = inferred;
                return;
            }
        }
        domain_ = KeyboardFocusDomain::TextControl;
        return;
    }
    if (context) {
        if (const auto inferred = inferFromElement(context->GetFocusElement());
            inferred != KeyboardFocusDomain::None) {
            domain_ = inferred;
            return;
        }
    }
    // Keep last claimed domain when nothing stronger is active.
}

} // namespace ArtCade::EditorNative
