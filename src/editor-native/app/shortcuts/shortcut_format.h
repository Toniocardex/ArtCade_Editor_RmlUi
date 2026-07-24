#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/shortcut_types.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Windows presentation: Primary → Ctrl.
std::string formatShortcutGesture(const ShortcutGesture& gesture);

// First catalog binding for the action (menu / tooltip). Empty if unbound.
std::string formatPrimaryShortcut(EditorActionId action);

// All catalog bindings for the action, catalog order.
std::vector<std::string> formatAllShortcuts(EditorActionId action);

} // namespace ArtCade::EditorNative
