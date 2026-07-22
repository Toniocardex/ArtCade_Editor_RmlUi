#include "editor-native/app/editor_coordinator.h"

#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/tile_palette_availability.h"

namespace ArtCade::EditorNative {

namespace {

bool selectedTilemapHasPaintableTileset(const EditorCoordinator& coordinator) {
    if (coordinator.isPlaying()
        || coordinator.state().centerWorkspaceMode != CenterWorkspaceMode::Scene) {
        return false;
    }
    const SceneInstanceDef* instance = coordinator.document().findInstanceInScene(
        coordinator.state().activeSceneId, coordinator.selection().primaryEntity);
    return instance && tilemapHasPaintableTileset(coordinator.document(), *instance);
}

constexpr EditorInvalidation kTilePaletteDockInvalidation =
    EditorInvalidation::Layout | EditorInvalidation::Viewport | EditorInvalidation::Inspector;

} // namespace

EditorOperationResult EditorCoordinator::apply(const ToggleConsoleIntent&) {
    uiState_.consoleVisible = !uiState_.consoleVisible;
    const EditorInvalidation inv = EditorInvalidation::Layout | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const ToggleTilePaletteDockIntent&) {
    if (!selectedTilemapHasPaintableTileset(*this)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    const bool visible = !uiState_.tilePaletteDockVisible;
    uiState_.tilePaletteDockVisible = visible;
    uiState_.tilePaletteDockAutoRevealEnabled = visible;
    accumulate(kTilePaletteDockInvalidation);
    return EditorOperationResult::success(kTilePaletteDockInvalidation);
}

EditorOperationResult EditorCoordinator::apply(const SetTilePaletteDockVisibleIntent& intent) {
    if (intent.visible && !selectedTilemapHasPaintableTileset(*this)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    const bool autoRevealEnabled = intent.visible;
    if (uiState_.tilePaletteDockVisible == intent.visible
        && uiState_.tilePaletteDockAutoRevealEnabled == autoRevealEnabled) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    uiState_.tilePaletteDockVisible = intent.visible;
    uiState_.tilePaletteDockAutoRevealEnabled = autoRevealEnabled;
    accumulate(kTilePaletteDockInvalidation);
    return EditorOperationResult::success(kTilePaletteDockInvalidation);
}

EditorOperationResult EditorCoordinator::apply(const RevealInspectorPropertyIntent& intent) {
    if (isPlaying()) {
        return finishIntent(EditorOperationResult::failure("Stop Play before editing tilemap properties"));
    }
    if (intent.entityId == INVALID_ENTITY) {
        return finishIntent(EditorOperationResult::failure("No entity to reveal in the Inspector"));
    }
    const SceneInstanceDef* inst =
        document_.findInstanceInScene(state_.activeSceneId, intent.entityId);
    if (!inst || !inst->tilemap.has_value()) {
        return finishIntent(EditorOperationResult::failure("Entity has no Tilemap component"));
    }
    if (intent.property != InspectorProperty::TilemapCellSize) {
        return finishIntent(EditorOperationResult::failure("Unknown Inspector property"));
    }

    if (state_.selection.primaryEntity != intent.entityId) {
        const EditorOperationResult selectResult = apply(SelectEntityIntent{intent.entityId});
        if (!selectResult.ok) return finishIntent(selectResult);
    }

    static std::uint64_t nextRevealRequestId = 1;
    uiState_.inspectorRevealRequest = InspectorRevealRequest{
        intent.entityId, intent.property, nextRevealRequestId++};
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

std::optional<InspectorRevealRequest> EditorCoordinator::takeInspectorRevealRequest() {
    std::optional<InspectorRevealRequest> request = uiState_.inspectorRevealRequest;
    uiState_.inspectorRevealRequest.reset();
    return request;
}

EditorOperationResult EditorCoordinator::apply(const ResizePanelIntent& intent) {
    if (!NumericValidation::isFinite(intent.size)) {
        return finishIntent(EditorOperationResult::failure("Panel size must be finite"));
    }
    switch (intent.panel) {
        case ResizePanelIntent::Panel::Left:
            uiState_.leftPanelWidth = clampLeftPanel(intent.size);
            break;
        case ResizePanelIntent::Panel::Right:
            uiState_.rightPanelWidth = clampRightPanel(intent.size);
            break;
        case ResizePanelIntent::Panel::Console:
            uiState_.consoleHeight = clampConsole(intent.size);
            break;
        case ResizePanelIntent::Panel::TilePaletteDock:
            uiState_.tilePaletteDockHeight = clampTilePaletteDock(intent.size);
            break;
    }
    // A splitter drag relays out the shell but refreshes no panel content.
    const EditorInvalidation inv = EditorInvalidation::Layout | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

// ----------------------------------------------------------------------------
// Console
// ----------------------------------------------------------------------------

} // namespace ArtCade::EditorNative
