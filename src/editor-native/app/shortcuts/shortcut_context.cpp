#include "editor-native/app/shortcuts/shortcut_context.h"

namespace ArtCade::EditorNative {

WorkspaceMask workspaceMaskFor(EditorWorkspaceKind workspace) {
    switch (workspace) {
    case EditorWorkspaceKind::Scene: return WorkspaceMask::Scene;
    case EditorWorkspaceKind::LogicBoard: return WorkspaceMask::LogicBoard;
    case EditorWorkspaceKind::ScriptEditor: return WorkspaceMask::ScriptEditor;
    }
    return WorkspaceMask::None;
}

FocusDomainMask focusMaskFor(KeyboardFocusDomain focus) {
    switch (focus) {
    case KeyboardFocusDomain::SceneViewport: return FocusDomainMask::SceneViewport;
    case KeyboardFocusDomain::Hierarchy: return FocusDomainMask::Hierarchy;
    case KeyboardFocusDomain::Assets: return FocusDomainMask::Assets;
    case KeyboardFocusDomain::Inspector: return FocusDomainMask::Inspector;
    case KeyboardFocusDomain::TilePalette: return FocusDomainMask::TilePalette;
    case KeyboardFocusDomain::LogicBoard:
    case KeyboardFocusDomain::LogicBoardProperties: return FocusDomainMask::LogicBoard;
    case KeyboardFocusDomain::ScriptTextEditor:
    case KeyboardFocusDomain::ScriptAssetList: return FocusDomainMask::ScriptTextEditor;
    case KeyboardFocusDomain::Console: return FocusDomainMask::Console;
    case KeyboardFocusDomain::ContextMenu: return FocusDomainMask::ContextMenu;
    case KeyboardFocusDomain::Modal: return FocusDomainMask::Modal;
    case KeyboardFocusDomain::TextControl: return FocusDomainMask::TextControl;
    default: return FocusDomainMask::None;
    }
}

OverlayMask overlayMaskFor(EditorOverlayKind overlay) {
    switch (overlay) {
    case EditorOverlayKind::None: return OverlayMask::NoneOnly;
    case EditorOverlayKind::SpriteAnimationEditor: return OverlayMask::SpriteAnimationEditor;
    case EditorOverlayKind::TilesetEditor: return OverlayMask::TilesetEditor;
    case EditorOverlayKind::GeneratedSfxEditor: return OverlayMask::GeneratedSfxEditor;
    }
    return OverlayMask::NoneOnly;
}

EditorModeMask modeMaskFor(bool playing) {
    return playing ? EditorModeMask::Play : EditorModeMask::Edit;
}

} // namespace ArtCade::EditorNative
