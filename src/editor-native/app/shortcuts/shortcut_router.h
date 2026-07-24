#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/shortcut_context.h"
#include "editor-native/app/shortcuts/shortcut_types.h"

namespace ArtCade::EditorNative {

struct ShortcutConsumption {
    bool consumePrimaryUntilRelease = false;
    ShortcutKey primaryKey = ShortcutKey::None;
};

struct ShortcutResolution {
    bool matched = false;
    EditorActionId action = EditorActionId::None;
    ShortcutGesture gesture{};
    ShortcutBindingId bindingId = 0;
    ShortcutConsumption requestedConsumption{};
};

struct ShortcutCatalogView {
    const ShortcutBinding* bindings = nullptr;
    std::size_t count = 0;
};

ShortcutResolution resolveShortcut(const KeyboardFrameSnapshot& keyboard,
                                   const EditorShortcutContext& context,
                                   const ShortcutCatalogView& catalog);

} // namespace ArtCade::EditorNative
