#pragma once

#include "editor-native/app/shortcuts/shortcut_types.h"

namespace ArtCade::EditorNative {

bool shortcutScopesOverlap(const ShortcutScope& a, const ShortcutScope& b);

struct ShortcutConflict {
    ShortcutGesture gesture{};
    EditorActionId first = EditorActionId::None;
    EditorActionId second = EditorActionId::None;
};

// Returns true when the catalog has no same-scope gesture conflicts.
bool validateShortcutCatalogConflicts(
    const ShortcutBinding* bindings,
    std::size_t count,
    ShortcutConflict* outConflict = nullptr);

} // namespace ArtCade::EditorNative
