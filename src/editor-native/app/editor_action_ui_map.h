#pragma once

#include "editor-native/app/shortcuts/editor_action.h"

#include <optional>
#include <string_view>

namespace ArtCade::EditorNative {

inline std::optional<EditorActionId> mapUiActionToEditorAction(std::string_view action) {
    if (action == "undo") return EditorActionId::Undo;
    if (action == "redo") return EditorActionId::Redo;
    if (action == "new-project") return EditorActionId::NewProject;
    if (action == "open-project") return EditorActionId::OpenProject;
    if (action == "save-project") return EditorActionId::SaveProject;
    if (action == "save-project-as") return EditorActionId::SaveProjectAs;
    if (action == "play-project") return EditorActionId::PlayProject;
    if (action == "play-current-scene") return EditorActionId::PlayCurrentScene;
    if (action == "stop") return EditorActionId::StopPlay;
    if (action == "duplicate-entity" || action == "clone-entity")
        return EditorActionId::SceneDuplicateInstance;
    if (action == "add-instance")
        return EditorActionId::SceneCreateInstanceOfSelectedType;
    if (action == "delete-entity")
        return EditorActionId::SceneDeleteSelection;
    if (action == "focus-selection")
        return EditorActionId::SceneFocusSelection;
    if (action == "select-tilemap-brush") return EditorActionId::TilemapBrushTool;
    if (action == "select-tilemap-eraser") return EditorActionId::TilemapEraserTool;
    if (action == "select-tilemap-picker") return EditorActionId::TilemapPickerTool;
    if (action == "select-tilemap-rectangle") return EditorActionId::TilemapRectangleTool;
    if (action == "select-tilemap-fill") return EditorActionId::TilemapFillTool;
    if (action == "copy-console-message" || action == "copy-console")
        return EditorActionId::ConsoleCopySelection;
    return std::nullopt;
}

inline ActionInvocationSource invocationSourceForUiAction(std::string_view action) {
    if (action == "undo" || action == "redo" || action == "stop"
        || action == "play-project" || action == "play-current-scene"
        || action == "select-tilemap-brush" || action == "select-tilemap-eraser"
        || action == "select-tilemap-picker" || action == "select-tilemap-rectangle"
        || action == "select-tilemap-fill")
        return ActionInvocationSource::Toolbar;
    return ActionInvocationSource::Menu;
}

} // namespace ArtCade::EditorNative
