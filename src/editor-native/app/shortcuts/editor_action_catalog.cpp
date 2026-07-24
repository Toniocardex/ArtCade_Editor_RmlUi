#include "editor-native/app/shortcuts/editor_action_catalog.h"

#include "editor-native/app/shortcuts/shortcut_conflicts.h"

#include <string>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

constexpr ShortcutScope kGlobalEdit{
    WorkspaceMask::Any,
    FocusDomainMask::Any,
    OverlayMask::NoneOnly,
    EditorModeMask::Edit,
    /*allowTextEditing*/ false,
    /*requireTilemapContext*/ false,
};

constexpr ShortcutScope kGlobalAnyMode{
    WorkspaceMask::Any,
    FocusDomainMask::Any,
    OverlayMask::NoneOnly,
    EditorModeMask::Any,
    false,
    false,
};

constexpr ShortcutScope kSceneAuthoring{
    WorkspaceMask::Scene,
    FocusDomainMask::AnySceneAuthoring,
    OverlayMask::NoneOnly,
    EditorModeMask::Edit,
    false,
    false,
};

constexpr ShortcutScope kSceneFocusSelection{
    WorkspaceMask::Scene,
    FocusDomainMask::AnySceneAuthoring,
    OverlayMask::NoneOnly,
    EditorModeMask::Edit,
    false,
    /*requireTilemapContext*/ false,
};

constexpr ShortcutScope kTilemapTools{
    WorkspaceMask::Scene,
    FocusDomainMask::AnySceneAuthoring | FocusDomainMask::TilePalette,
    OverlayMask::NoneOnly,
    EditorModeMask::Edit,
    false,
    /*requireTilemapContext*/ true,
};

constexpr ShortcutScope kConsoleCopy{
    WorkspaceMask::Any,
    FocusDomainMask::Console,
    OverlayMask::NoneOnly,
    EditorModeMask::Any,
    false,
    false,
};

constexpr EditorActionDescriptor kDescriptors[] = {
    {EditorActionId::NewProject, "file.new", "New Project", "",
     EditorActionCategory::File, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::OpenProject, "file.open", "Open Project", "",
     EditorActionCategory::File, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::SaveProject, "file.save", "Save", "",
     EditorActionCategory::File, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::SaveProjectAs, "file.save_as", "Save As", "",
     EditorActionCategory::File, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::ExportWindows, "file.export_windows", "Export Windows", "",
     EditorActionCategory::File, PendingEditPolicy::CommitThenExecute},

    {EditorActionId::Undo, "history.undo", "Undo", "",
     EditorActionCategory::History, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::Redo, "history.redo", "Redo", "",
     EditorActionCategory::History, PendingEditPolicy::SuppressWhileEditing},

    {EditorActionId::PlayProject, "run.play_project", "Play Project", "",
     EditorActionCategory::Run, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::PlayCurrentScene, "run.play_scene", "Play Current Scene", "",
     EditorActionCategory::Run, PendingEditPolicy::CommitThenExecute},
    {EditorActionId::StopPlay, "run.stop", "Stop", "",
     EditorActionCategory::Run, PendingEditPolicy::Ignore},

    {EditorActionId::SceneDuplicateInstance, "scene.duplicate_instance",
     "Duplicate Instance", "", EditorActionCategory::Scene,
     PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::SceneCreateInstanceOfSelectedType,
     "scene.create_instance_of_selected_type", "Create Instance of Type", "",
     EditorActionCategory::Scene, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::SceneRenameSelection, "scene.rename_selection", "Rename", "",
     EditorActionCategory::Scene, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::SceneDeleteSelection, "scene.delete_selection", "Delete", "",
     EditorActionCategory::Scene, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::SceneFocusSelection, "scene.focus_selection", "Focus Selection",
     "", EditorActionCategory::Scene, PendingEditPolicy::SuppressWhileEditing},

    {EditorActionId::TilemapBrushTool, "tilemap.tool.brush", "Brush", "",
     EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilemapEraserTool, "tilemap.tool.eraser", "Eraser", "",
     EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilemapPickerTool, "tilemap.tool.picker", "Picker", "",
     EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilemapRectangleTool, "tilemap.tool.rectangle", "Rectangle", "",
     EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilemapFillTool, "tilemap.tool.fill", "Fill", "",
     EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilePaletteFitContent, "tilemap.fit_content", "Fit Palette Content",
     "", EditorActionCategory::Tilemap, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::TilePaletteFitSelection, "tilemap.fit_selection",
     "Fit Palette Selection", "", EditorActionCategory::Tilemap,
     PendingEditPolicy::SuppressWhileEditing},

    {EditorActionId::ConsoleCopySelection, "console.copy_selection", "Copy Message",
     "", EditorActionCategory::Console, PendingEditPolicy::Ignore},

    {EditorActionId::ShowKeyboardShortcuts, "help.keyboard_shortcuts",
     "Keyboard Shortcuts…", "Browse the available editor keyboard shortcuts",
     EditorActionCategory::Help, PendingEditPolicy::Ignore},
    {EditorActionId::ShowAboutArtCade, "help.about", "About ArtCade…",
     "Show application and runtime version information",
     EditorActionCategory::Help, PendingEditPolicy::Ignore},

    {EditorActionId::CancelCurrentOperation, "ui.cancel", "Cancel", "",
     EditorActionCategory::View, PendingEditPolicy::Ignore},

    // Future placeholders (no bindings in Phase 1/2)
    {EditorActionId::LogicDeleteSelection, "logic.delete_selection", "Delete Selection",
     "", EditorActionCategory::LogicBoard, PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::LogicDuplicateSelection, "logic.duplicate_selection",
     "Duplicate Selection", "", EditorActionCategory::LogicBoard,
     PendingEditPolicy::SuppressWhileEditing},
    {EditorActionId::ScriptFind, "script.find", "Find", "",
     EditorActionCategory::ScriptEditor, PendingEditPolicy::Ignore},
};

constexpr ShortcutBinding kBindings[] = {
    {1, EditorActionId::Undo,
     {ShortcutKey::Z, ShortcutModifier::Primary}, kGlobalEdit, ShortcutTrigger::PressOnce},
    {2, EditorActionId::Redo,
     {ShortcutKey::Y, ShortcutModifier::Primary}, kGlobalEdit, ShortcutTrigger::PressOnce},
    {3, EditorActionId::Redo,
     {ShortcutKey::Z, ShortcutModifier::Primary | ShortcutModifier::Shift}, kGlobalEdit,
     ShortcutTrigger::PressOnce},

    {4, EditorActionId::SaveProject,
     {ShortcutKey::S, ShortcutModifier::Primary}, kGlobalEdit, ShortcutTrigger::PressOnce},
    {5, EditorActionId::SaveProjectAs,
     {ShortcutKey::S, ShortcutModifier::Primary | ShortcutModifier::Shift}, kGlobalEdit,
     ShortcutTrigger::PressOnce},

    {6, EditorActionId::PlayProject,
     {ShortcutKey::F5, ShortcutModifier::None}, kGlobalEdit, ShortcutTrigger::PressOnce},
    {7, EditorActionId::PlayCurrentScene,
     {ShortcutKey::F6, ShortcutModifier::None}, kGlobalEdit, ShortcutTrigger::PressOnce},
    {8, EditorActionId::StopPlay,
     {ShortcutKey::F8, ShortcutModifier::None}, kGlobalAnyMode, ShortcutTrigger::PressOnce},

    {9, EditorActionId::SceneDuplicateInstance,
     {ShortcutKey::D, ShortcutModifier::Primary}, kSceneAuthoring, ShortcutTrigger::PressOnce},
    {10, EditorActionId::SceneCreateInstanceOfSelectedType,
     {ShortcutKey::D, ShortcutModifier::Primary | ShortcutModifier::Shift}, kSceneAuthoring,
     ShortcutTrigger::PressOnce},
    {11, EditorActionId::SceneRenameSelection,
     {ShortcutKey::F2, ShortcutModifier::None}, kSceneAuthoring, ShortcutTrigger::PressOnce},
    {12, EditorActionId::SceneDeleteSelection,
     {ShortcutKey::Delete, ShortcutModifier::None}, kSceneAuthoring, ShortcutTrigger::PressOnce},
    {13, EditorActionId::SceneFocusSelection,
     {ShortcutKey::F, ShortcutModifier::None}, kSceneFocusSelection, ShortcutTrigger::PressOnce},

    {14, EditorActionId::TilemapBrushTool,
     {ShortcutKey::B, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {15, EditorActionId::TilemapEraserTool,
     {ShortcutKey::E, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {16, EditorActionId::TilemapPickerTool,
     {ShortcutKey::I, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {17, EditorActionId::TilemapRectangleTool,
     {ShortcutKey::R, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {18, EditorActionId::TilemapFillTool,
     {ShortcutKey::F, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {19, EditorActionId::TilePaletteFitContent,
     {ShortcutKey::Home, ShortcutModifier::None}, kTilemapTools, ShortcutTrigger::PressOnce},
    {20, EditorActionId::TilePaletteFitSelection,
     {ShortcutKey::Home, ShortcutModifier::Shift}, kTilemapTools, ShortcutTrigger::PressOnce},

    {21, EditorActionId::ConsoleCopySelection,
     {ShortcutKey::C, ShortcutModifier::Primary}, kConsoleCopy, ShortcutTrigger::PressOnce},

    {22, EditorActionId::ShowKeyboardShortcuts,
     {ShortcutKey::F1, ShortcutModifier::None}, kGlobalAnyMode, ShortcutTrigger::PressOnce},

    {23, EditorActionId::CancelCurrentOperation,
     {ShortcutKey::Escape, ShortcutModifier::None}, kGlobalAnyMode, ShortcutTrigger::PressOnce},
};

} // namespace

const EditorActionDescriptor* findActionDescriptor(EditorActionId id) {
    for (const auto& d : kDescriptors)
        if (d.id == id) return &d;
    return nullptr;
}

const EditorActionDescriptor* findActionDescriptorByStableKey(std::string_view stableKey) {
    for (const auto& d : kDescriptors)
        if (d.stableKey == stableKey) return &d;
    return nullptr;
}

std::size_t actionDescriptorCount() {
    return sizeof(kDescriptors) / sizeof(kDescriptors[0]);
}

const EditorActionDescriptor* actionDescriptors() { return kDescriptors; }

std::size_t shortcutBindingCount() {
    return sizeof(kBindings) / sizeof(kBindings[0]);
}

const ShortcutBinding* shortcutBindings() { return kBindings; }

bool validateEditorActionCatalog(std::string* errorOut) {
    std::unordered_set<std::string> keys;
    for (const auto& d : kDescriptors) {
        if (d.stableKey.empty() || d.label.empty()) {
            if (errorOut) *errorOut = "descriptor missing key or label";
            return false;
        }
        if (!keys.insert(std::string(d.stableKey)).second) {
            if (errorOut) *errorOut = "duplicate stable key: " + std::string(d.stableKey);
            return false;
        }
    }
    for (const auto& b : kBindings) {
        if (!findActionDescriptor(b.action)) {
            if (errorOut) *errorOut = "binding references unknown action";
            return false;
        }
    }
    ShortcutConflict conflict;
    if (!validateShortcutCatalogConflicts(kBindings, shortcutBindingCount(), &conflict)) {
        if (errorOut) *errorOut = "same-scope shortcut conflict";
        return false;
    }
    return true;
}

} // namespace ArtCade::EditorNative
