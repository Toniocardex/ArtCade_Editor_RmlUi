#pragma once

#include <cstdint>
#include <string_view>

namespace ArtCade::EditorNative {

enum class EditorActionId : std::uint16_t {
    None = 0,

    // File
    NewProject,
    OpenProject,
    SaveProject,
    SaveProjectAs,
    ExportWindows,

    // History
    Undo,
    Redo,

    // Run
    PlayProject,
    PlayCurrentScene,
    StopPlay,

    // Workspace (future bindings)
    OpenSceneWorkspace,
    OpenLogicBoardWorkspace,
    OpenScriptEditorWorkspace,

    // Scene
    SceneDuplicateInstance,
    SceneCreateInstanceOfSelectedType,
    SceneRenameSelection,
    SceneDeleteSelection,
    SceneFocusSelection,

    // Tilemap
    TilemapBrushTool,
    TilemapEraserTool,
    TilemapPickerTool,
    TilemapRectangleTool,
    TilemapFillTool,
    TilePaletteFitContent,
    TilePaletteFitSelection,

    // Console
    ConsoleCopySelection,

    // Help
    ShowKeyboardShortcuts,
    ShowAboutArtCade,

    // UI
    CancelCurrentOperation,

    // Future categories (no Phase 1/2 bindings required)
    LogicDeleteSelection,
    LogicDuplicateSelection,
    ScriptFind,

    Count
};

enum class EditorActionCategory : std::uint8_t {
    File,
    History,
    Run,
    Workspace,
    Scene,
    Tilemap,
    LogicBoard,
    ScriptEditor,
    View,
    Console,
    Help,
};

enum class PendingEditPolicy : std::uint8_t {
    Ignore,
    SuppressWhileEditing,
    CommitThenExecute,
    CancelThenExecute,
};

enum class ActionInvocationSource : std::uint8_t {
    Shortcut,
    Menu,
    Toolbar,
    CommandPalette,
};

struct EditorActionDescriptor {
    EditorActionId id = EditorActionId::None;
    std::string_view stableKey;
    std::string_view label;
    std::string_view description;
    EditorActionCategory category = EditorActionCategory::History;
    PendingEditPolicy pendingEditPolicy = PendingEditPolicy::Ignore;
};

} // namespace ArtCade::EditorNative
