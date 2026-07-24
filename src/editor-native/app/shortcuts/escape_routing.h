#pragma once

#include "editor-native/app/shortcuts/shortcut_context.h"

#include <cstdint>

namespace ArtCade::EditorNative {

enum class EscapeOwner : std::uint8_t {
    None,
    Modal,
    LogicKeyCapture,
    ContextMenu,
    InlineRename,
    BackgroundOpacityDraft,
    TilemapOperation,
    ViewportDrag,
    TemporaryTool,
    LegacyOverlayHandler,
};

struct EscapeContext {
    bool modalOpen = false;
    bool logicKeyCapture = false;
    bool contextMenuOpen = false;
    bool inlineRenameActive = false;
    bool backgroundOpacityDraft = false;
    bool tilemapOperationActive = false;
    bool viewportDragActive = false;
    bool temporaryToolActive = false;
    bool overlayEditorOpen = false;
};

EscapeOwner resolveEscapeOwner(const EscapeContext& context);

} // namespace ArtCade::EditorNative
