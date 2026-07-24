#pragma once

#include "editor-native/app/shortcuts/shortcut_types.h"

namespace ArtCade::EditorNative {

enum class EditorWorkspaceKind : std::uint8_t {
    Scene,
    LogicBoard,
    ScriptEditor,
};

enum class EditorOverlayKind : std::uint8_t {
    None,
    SpriteAnimationEditor,
    TilesetEditor,
    GeneratedSfxEditor,
};

enum class KeyboardFocusDomain : std::uint8_t {
    None,
    MenuBar,
    Modal,
    ContextMenu,
    ShortcutCapture,
    SceneViewport,
    Hierarchy,
    Assets,
    Inspector,
    TilePalette,
    LogicBoard,
    LogicBoardProperties,
    ScriptAssetList,
    ScriptTextEditor,
    SpriteAnimationCanvas,
    TilesetCanvas,
    GeneratedSfxEditor,
    Console,
    TextControl,
};

struct EditorShortcutContext {
    EditorWorkspaceKind workspace = EditorWorkspaceKind::Scene;
    EditorOverlayKind overlay = EditorOverlayKind::None;
    KeyboardFocusDomain focus = KeyboardFocusDomain::None;

    bool windowFocused = true;
    bool playing = false;
    bool modalOpen = false;
    bool popupOpen = false;
    bool textEditing = false;
    bool exclusiveKeyCapture = false;
    bool activeGesture = false;
    bool tilemapEditingAvailable = false;
    bool tilemapOperationActive = false;
};

WorkspaceMask workspaceMaskFor(EditorWorkspaceKind workspace);
FocusDomainMask focusMaskFor(KeyboardFocusDomain focus);
OverlayMask overlayMaskFor(EditorOverlayKind overlay);
EditorModeMask modeMaskFor(bool playing);

} // namespace ArtCade::EditorNative
