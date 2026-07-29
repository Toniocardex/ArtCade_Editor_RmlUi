#include "editor-native/app/editor_action_dispatcher.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/project_session_controller.h"
#include "editor-native/app/scene_view_interaction.h"
#include "editor-native/app/shortcuts/editor_action_catalog.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/model/tile_palette_availability.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/scene_grid.h"

namespace ArtCade::EditorNative {

EditorActionDispatcher::EditorActionDispatcher(EditorCoordinator& coordinator,
                                               EditorUi& ui,
                                               ProjectSessionController& session)
    : coordinator_(coordinator), ui_(ui), session_(session) {}

EditorActionResult EditorActionDispatcher::invoke(const EditorActionRequest& request,
                                                  const EditorActionContext& context) {
    const EditorActionState state = resolveActionState(request.action, context);
    if (!state.enabled)
        return EditorActionResult::disabled(state.disabledReason);
    return execute(request, context);
}

EditorActionResult EditorActionDispatcher::execute(const EditorActionRequest& request,
                                                   const EditorActionContext& context) {
    if (request.action == EditorActionId::None)
        return EditorActionResult::noOp();

    const EditorActionDescriptor* desc = findActionDescriptor(request.action);
    if (!desc) return EditorActionResult::failed("Unknown action");

    // ADR-0031 A2.2: Object Variable drafts are authoring scratch state, not
    // pending changes to carry across Replace Project or into Play. Other
    // focused fields retain their existing CommitThenExecute policy.
    if (request.action == EditorActionId::NewProject
        || request.action == EditorActionId::OpenProject
        || request.action == EditorActionId::PlayProject
        || request.action == EditorActionId::PlayCurrentScene) {
        ui_.discardObjectVariableDraft();
    }

    switch (desc->pendingEditPolicy) {
    case PendingEditPolicy::Ignore:
        break;
    case PendingEditPolicy::SuppressWhileEditing:
        break;
    case PendingEditPolicy::CommitThenExecute:
        if (!ui_.resolvePendingEdits().resolved())
            return EditorActionResult::failed("Pending edits could not be resolved");
        break;
    case PendingEditPolicy::CancelThenExecute:
        ui_.resolvePendingEdits();
        break;
    }

    return executeEnabled(request.action, context);
}

EditorActionResult EditorActionDispatcher::executeEnabled(
    EditorActionId action, const EditorActionContext& context) {
    switch (action) {
    case EditorActionId::Undo:
        if (tryScriptUndo_ && tryScriptUndo_())
            return EditorActionResult::executed();
        coordinator_.undo();
        return EditorActionResult::executed();
    case EditorActionId::Redo:
        if (tryScriptRedo_ && tryScriptRedo_())
            return EditorActionResult::executed();
        coordinator_.redo();
        return EditorActionResult::executed();

    case EditorActionId::NewProject:
        session_.requestNewProject();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::OpenProject:
        session_.requestOpenProject();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::SaveProject:
        session_.requestSave();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::SaveProjectAs:
        session_.requestSaveAs();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::PlayProject:
        session_.requestPlayProject();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::PlayCurrentScene:
        session_.requestPlayCurrentScene();
        return EditorActionResult::startedWorkflow();
    case EditorActionId::StopPlay:
        coordinator_.stopPlaying();
        return EditorActionResult::executed();

    case EditorActionId::SceneDuplicateInstance: {
        if (duplicateSelectedEntity(coordinator_).ok) {
            const EntityId newId = coordinator_.selection().primaryEntity;
            const SceneId& sceneId = coordinator_.state().activeSceneId;
            std::string layerId;
            if (const SceneInstanceDef* inst =
                    coordinator_.document().findInstanceInScene(sceneId, newId)) {
                layerId = coordinator_.document().effectiveLayerId(sceneId, *inst);
            }
            ui_.requestHierarchyReveal(sceneId, newId, layerId);
            return EditorActionResult::executed();
        }
        return EditorActionResult::failed("Duplicate failed");
    }
    case EditorActionId::SceneCreateInstanceOfSelectedType:
        addInstanceOfSelectedType(coordinator_);
        return EditorActionResult::executed();
    case EditorActionId::SceneDeleteSelection:
        deleteSelectedEntity(coordinator_);
        return EditorActionResult::executed();
    case EditorActionId::SceneFocusSelection:
        if (focusSelection_ && focusSelection_())
            return EditorActionResult::executed();
        return EditorActionResult::failed("Focus Selection unavailable");

    case EditorActionId::TilemapBrushTool:
        coordinator_.apply(SetActiveToolIntent{EditorTool::Brush});
        return EditorActionResult::executed();
    case EditorActionId::TilemapEraserTool:
        coordinator_.apply(SetActiveToolIntent{EditorTool::Eraser});
        return EditorActionResult::executed();
    case EditorActionId::TilemapPickerTool:
        coordinator_.apply(SetActiveToolIntent{EditorTool::Picker});
        return EditorActionResult::executed();
    case EditorActionId::TilemapRectangleTool:
        coordinator_.apply(SetActiveToolIntent{EditorTool::Rectangle});
        return EditorActionResult::executed();
    case EditorActionId::TilemapFillTool:
        coordinator_.apply(SetActiveToolIntent{EditorTool::Fill});
        return EditorActionResult::executed();

    case EditorActionId::TilePaletteFitContent:
    case EditorActionId::TilePaletteFitSelection: {
        const SceneInstanceDef* tmInst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (!tmInst || !tilemapHasPaintableTileset(coordinator_.document(), *tmInst))
            return EditorActionResult::failed("No paintable tileset");
        const AssetId tilesetId = tmInst->tilemap->tilesetAssetId;
        if (!coordinator_.uiState().tilePaletteDockVisible)
            coordinator_.apply(SetTilePaletteDockVisibleIntent{true});
        coordinator_.apply(RequestTilePaletteFitIntent{
            tilesetId,
            action == EditorActionId::TilePaletteFitSelection
                ? TilePaletteFitKind::Selection
                : TilePaletteFitKind::Content});
        return EditorActionResult::executed();
    }

    case EditorActionId::ConsoleCopySelection:
        if (!ui_.copySelectedConsoleMessage())
            return EditorActionResult::noOp();
        return EditorActionResult::executed();

    case EditorActionId::ShowKeyboardShortcuts:
        ui_.openHelpKeyboardShortcuts(context);
        return EditorActionResult::executed();
    case EditorActionId::ShowAboutArtCade:
        ui_.openHelpAbout();
        return EditorActionResult::executed();

    case EditorActionId::CancelCurrentOperation: {
        switch (resolveEscapeOwner(escapeContext_)) {
        case EscapeOwner::Modal:
            if (ui_.helpDialogOpen()) ui_.closeHelp();
            else ui_.cancelConfirm();
            return EditorActionResult::executed();
        case EscapeOwner::LogicKeyCapture:
            ui_.cancelLogicKeyCapture();
            return EditorActionResult::executed();
        case EscapeOwner::ContextMenu:
            // ADR-0034/0035: an open Inspector or Logic Board dropdown shares
            // this tier with the floating context menus below (see
            // hasOpenContextMenu()); all three calls are safe to make
            // unconditionally, each a no-op when its own thing isn't open.
            ui_.hideContextMenus();
            ui_.dismissInspectorTransientMenus();
            ui_.dismissLogicBoardTransientMenus();
            return EditorActionResult::executed();
        case EscapeOwner::BackgroundOpacityDraft:
            ui_.cancelSceneBackgroundOpacityDrag();
            return EditorActionResult::executed();
        case EscapeOwner::TilemapOperation:
            coordinator_.cancelPendingTilemapGesture();
            return EditorActionResult::executed();
        case EscapeOwner::ViewportDrag:
            if (clearViewportDrag_) clearViewportDrag_();
            return EditorActionResult::executed();
        case EscapeOwner::TemporaryTool:
            if (coordinator_.state().tilemapEditor.temporaryToolOverride)
                coordinator_.apply(EndTemporaryToolOverrideIntent{});
            return EditorActionResult::executed();
        case EscapeOwner::LegacyOverlayHandler:
            if (closeLegacyOverlay_) closeLegacyOverlay_();
            return EditorActionResult::executed();
        case EscapeOwner::None:
            routeGlobalEscape(coordinator_);
            return EditorActionResult::executed();
        }
        return EditorActionResult::noOp();
    }

    default:
        return EditorActionResult::failed("Action not implemented");
    }
}

} // namespace ArtCade::EditorNative
