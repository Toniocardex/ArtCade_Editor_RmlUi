#pragma once

#include "editor-native/app/shortcuts/editor_action_state.h"
#include "editor-native/app/shortcuts/shortcut_context.h"

namespace ArtCade::EditorNative {

class EditorCoordinator;
class EditorUi;

// Projects live coordinator/UI flags into the pure ActionStateResolver input.
EditorActionContext buildEditorActionContext(
    const EditorCoordinator& coordinator,
    const EditorUi& ui,
    KeyboardFocusDomain focus,
    EditorOverlayKind overlay,
    bool textEditing,
    bool modalOpen,
    bool popupOpen,
    bool exclusiveCapture);

} // namespace ArtCade::EditorNative
