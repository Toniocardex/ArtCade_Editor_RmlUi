#include "editor-native/app/shortcuts/editor_action_state.h"

namespace ArtCade::EditorNative {

namespace {

EditorActionState disabled(std::string reason) {
    EditorActionState s;
    s.enabled = false;
    s.disabledReason = std::move(reason);
    return s;
}

bool sceneAuthoringOk(const EditorActionContext& ctx) {
    return ctx.workspace == EditorWorkspaceKind::Scene
        && !ctx.playing
        && !ctx.modalOpen
        && !ctx.textEditing
        && ctx.overlay == EditorOverlayKind::None;
}

} // namespace

EditorActionState resolveActionState(EditorActionId action,
                                     const EditorActionContext& context) {
    if (context.modalOpen || context.exclusiveCapture) {
        if (action == EditorActionId::CancelCurrentOperation)
            return {};
        // Help host is a blocking modal, but Help actions may switch pages.
        if (context.helpDialogOpen
            && (action == EditorActionId::ShowKeyboardShortcuts
                || action == EditorActionId::ShowAboutArtCade)) {
            // Fall through to action-specific rules below.
        } else {
            return disabled("A modal or key capture is active");
        }
    }

    switch (action) {
    case EditorActionId::Undo:
        if (context.textEditing) return disabled("Text editing owns Undo");
        if (!context.canUndo) return disabled("Nothing to undo");
        return {};
    case EditorActionId::Redo:
        if (context.textEditing) return disabled("Text editing owns Redo");
        if (!context.canRedo) return disabled("Nothing to redo");
        return {};

    case EditorActionId::NewProject:
    case EditorActionId::OpenProject:
    case EditorActionId::ExportWindows:
        return {};

    case EditorActionId::SaveProject:
    case EditorActionId::SaveProjectAs:
        if (context.textEditing
            && context.focus == KeyboardFocusDomain::ScriptTextEditor)
            return disabled("Script editor owns Save"); // Phase 4 owns it
        if (!context.projectAvailable) return disabled("No project");
        return {};

    case EditorActionId::PlayProject:
    case EditorActionId::PlayCurrentScene:
        if (context.playing) return disabled("Already playing");
        if (!context.projectAvailable) return disabled("No project");
        return {};
    case EditorActionId::StopPlay:
        if (!context.playing) return disabled("Not playing");
        return {};

    case EditorActionId::SceneDuplicateInstance:
    case EditorActionId::SceneCreateInstanceOfSelectedType:
    case EditorActionId::SceneRenameSelection:
    case EditorActionId::SceneDeleteSelection:
    case EditorActionId::SceneFocusSelection:
        if (!sceneAuthoringOk(context)) {
            if (context.playing) return disabled("Cannot edit while playing");
            return disabled("Scene authoring unavailable");
        }
        if (!context.selectedEntityAvailable)
            return disabled("Select an instance");
        if (context.selectedEntityLayerLocked
            && action != EditorActionId::SceneFocusSelection)
            return disabled("The selected instance belongs to a locked layer");
        return {};

    case EditorActionId::TilemapBrushTool:
    case EditorActionId::TilemapEraserTool:
    case EditorActionId::TilemapPickerTool:
    case EditorActionId::TilemapRectangleTool:
    case EditorActionId::TilemapFillTool:
        if (!sceneAuthoringOk(context)) return disabled("Tilemap tools unavailable");
        if (!context.tilemapEditingAvailable)
            return disabled("Select a tilemap instance");
        if (context.tilemapOperationActive)
            return disabled("Finish the current tilemap operation");
        return {};

    case EditorActionId::TilePaletteFitContent:
    case EditorActionId::TilePaletteFitSelection:
        if (!sceneAuthoringOk(context)) return disabled("Palette fit unavailable");
        if (!context.tilePaletteAvailable)
            return disabled("No paintable tileset");
        return {};

    case EditorActionId::ConsoleCopySelection:
        if (context.focus != KeyboardFocusDomain::Console)
            return disabled("Console is not focused");
        if (!context.consoleSelectionAvailable)
            return disabled("No console message selected");
        return {};

    case EditorActionId::ShowKeyboardShortcuts:
    case EditorActionId::ShowAboutArtCade:
        return {};

    case EditorActionId::CancelCurrentOperation:
        return {};

    default:
        return disabled("Action not available");
    }
}

} // namespace ArtCade::EditorNative
