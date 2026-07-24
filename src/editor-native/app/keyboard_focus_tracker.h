#pragma once

#include "editor-native/app/shortcuts/shortcut_context.h"

namespace Rml { class Context; class Element; }

namespace ArtCade::EditorNative {

// Presentation-only keyboard focus domain (not EditorUiState).
class KeyboardFocusTracker {
public:
    KeyboardFocusDomain domain() const { return domain_; }

    void clear();
    void claim(KeyboardFocusDomain domain);
    void claimFromElement(Rml::Element* element);

    // Infer from Rml focus ancestry / script focus / workspace. Clears on
    // modal, window blur, or workspace change when requested.
    void sync(Rml::Context* context,
              bool windowFocused,
              bool modalOpen,
              bool scriptEditorFocused,
              bool textFieldFocused,
              EditorWorkspaceKind workspace,
              bool workspaceChanged);

private:
    KeyboardFocusDomain domain_ = KeyboardFocusDomain::None;
    EditorWorkspaceKind workspace_ = EditorWorkspaceKind::Scene;
};

} // namespace ArtCade::EditorNative
