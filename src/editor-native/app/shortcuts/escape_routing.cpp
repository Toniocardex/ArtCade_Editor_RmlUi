#include "editor-native/app/shortcuts/escape_routing.h"

namespace ArtCade::EditorNative {

EscapeOwner resolveEscapeOwner(const EscapeContext& context) {
    if (context.modalOpen) return EscapeOwner::Modal;
    if (context.logicKeyCapture) return EscapeOwner::LogicKeyCapture;
    if (context.contextMenuOpen) return EscapeOwner::ContextMenu;
    if (context.inlineRenameActive) return EscapeOwner::InlineRename;
    if (context.backgroundOpacityDraft) return EscapeOwner::BackgroundOpacityDraft;
    if (context.tilemapOperationActive) return EscapeOwner::TilemapOperation;
    if (context.viewportDragActive) return EscapeOwner::ViewportDrag;
    if (context.temporaryToolActive) return EscapeOwner::TemporaryTool;
    if (context.overlayEditorOpen) return EscapeOwner::LegacyOverlayHandler;
    return EscapeOwner::None;
}

} // namespace ArtCade::EditorNative
