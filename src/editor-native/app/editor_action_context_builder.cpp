#include "editor-native/app/editor_action_context_builder.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/script_editor_state.h"
#include "editor-native/model/tile_palette_availability.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/scene_grid.h"

namespace ArtCade::EditorNative {

EditorActionContext buildEditorActionContext(
    const EditorCoordinator& coordinator,
    const EditorUi& ui,
    KeyboardFocusDomain focus,
    EditorOverlayKind overlay,
    bool textEditing,
    bool modalOpen,
    bool popupOpen,
    bool exclusiveCapture) {
    EditorActionContext ctx;
    switch (coordinator.state().centerWorkspaceMode) {
    case CenterWorkspaceMode::Logic:
        ctx.workspace = EditorWorkspaceKind::LogicBoard;
        break;
    case CenterWorkspaceMode::Script:
        ctx.workspace = EditorWorkspaceKind::ScriptEditor;
        break;
    case CenterWorkspaceMode::Scene:
    default:
        ctx.workspace = EditorWorkspaceKind::Scene;
        break;
    }
    ctx.focus = focus;
    ctx.overlay = overlay;
    ctx.playing = coordinator.isPlaying();
    ctx.textEditing = textEditing;
    ctx.modalOpen = modalOpen;
    ctx.popupOpen = popupOpen;
    ctx.exclusiveCapture = exclusiveCapture;
    ctx.helpDialogOpen = ui.helpDialogOpen();
    ctx.canUndo = coordinator.canUndo();
    ctx.canRedo = coordinator.canRedo();
    // Script workspace toolbar/menu history is buffer-local until Phase 4.
    if (coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Script) {
        if (const ScriptEditorBuffer* buffer = coordinator.state().scriptEditor.active()) {
            ctx.canUndo = buffer->canUndo();
            ctx.canRedo = buffer->canRedo();
        }
    }
    ctx.projectAvailable = true;
    ctx.selectedEntityAvailable =
        coordinator.selection().primaryEntity != INVALID_ENTITY;
    if (ctx.selectedEntityAvailable) {
        if (const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
                coordinator.state().activeSceneId, coordinator.selection().primaryEntity)) {
            ctx.selectedEntityLayerLocked = coordinator.document().isInstanceLayerLocked(
                coordinator.state().activeSceneId, *inst);
        }
    }
    ctx.tilemapEditingAvailable = selectionSupportsTilemapEditing(
        coordinator.document(), coordinator.state(), coordinator.state().activeSceneId);
    ctx.tilemapOperationActive =
        coordinator.state().tilemapEditor.pendingStroke.has_value()
        || coordinator.state().tilemapEditor.pendingRectangle.has_value();
    if (ctx.tilemapEditingAvailable) {
        if (const SceneInstanceDef* tmInst = coordinator.document().findInstanceInScene(
                coordinator.state().activeSceneId, coordinator.selection().primaryEntity)) {
            ctx.tilePaletteAvailable =
                tilemapHasPaintableTileset(coordinator.document(), *tmInst);
        }
    }
    ctx.consoleSelectionAvailable = ui.hasConsoleMessageSelected();
    return ctx;
}

} // namespace ArtCade::EditorNative
