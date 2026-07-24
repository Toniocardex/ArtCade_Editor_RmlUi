#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/editor_action_state.h"
#include "editor-native/app/shortcuts/shortcut_context.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArtCade::EditorNative {

enum class KeyboardShortcutsFilter : std::uint8_t {
    All,
    General,
    Scene,
    LogicBoard,
    ScriptEditor,
};

struct KeyboardShortcutProjectionInput {
    KeyboardShortcutsFilter filter = KeyboardShortcutsFilter::All;
    std::string_view search;
    EditorActionContext availabilityContext{};
};

struct KeyboardShortcutListItem {
    EditorActionId action = EditorActionId::None;
    std::string label;
    std::string description;
    std::string categoryLabel;
    std::vector<std::string> formattedGestures;
    bool currentlyAvailable = false;
};

// Strip modal/popup/text/capture and set a default focus for the live workspace so
// "currently available" answers workspace availability, not modal blocking.
EditorActionContext makeShortcutHelpEvaluationContext(const EditorActionContext& live);

KeyboardShortcutsFilter defaultKeyboardShortcutsFilterFor(EditorWorkspaceKind workspace);

std::vector<KeyboardShortcutListItem> buildKeyboardShortcutProjection(
    const KeyboardShortcutProjectionInput& input);

} // namespace ArtCade::EditorNative
