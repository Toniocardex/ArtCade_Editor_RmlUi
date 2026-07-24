#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/shortcut_context.h"

#include <string>

namespace ArtCade::EditorNative {

struct EditorActionContext {
    EditorWorkspaceKind workspace = EditorWorkspaceKind::Scene;
    KeyboardFocusDomain focus = KeyboardFocusDomain::None;
    EditorOverlayKind overlay = EditorOverlayKind::None;

    bool playing = false;
    bool textEditing = false;
    bool modalOpen = false;
    bool popupOpen = false;
    bool exclusiveCapture = false;
    bool helpDialogOpen = false;

    bool canUndo = false;
    bool canRedo = false;

    bool projectAvailable = false;
    bool selectedEntityAvailable = false;
    bool selectedEntityLayerLocked = false;

    bool tilemapEditingAvailable = false;
    bool tilemapOperationActive = false;
    bool tilePaletteAvailable = false;

    bool consoleSelectionAvailable = false;
};

struct EditorActionState {
    bool visible = true;
    bool enabled = true;
    bool checked = false;
    std::string disabledReason;
};

EditorActionState resolveActionState(EditorActionId action,
                                     const EditorActionContext& context);

} // namespace ArtCade::EditorNative
