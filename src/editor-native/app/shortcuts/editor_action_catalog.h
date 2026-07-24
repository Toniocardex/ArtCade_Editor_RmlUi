#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/shortcut_types.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace ArtCade::EditorNative {

const EditorActionDescriptor* findActionDescriptor(EditorActionId id);
const EditorActionDescriptor* findActionDescriptorByStableKey(std::string_view stableKey);
std::size_t actionDescriptorCount();
const EditorActionDescriptor* actionDescriptors();

std::size_t shortcutBindingCount();
const ShortcutBinding* shortcutBindings();

// Validates unique stable keys, valid action refs, and no same-scope conflicts.
bool validateEditorActionCatalog(std::string* errorOut = nullptr);

} // namespace ArtCade::EditorNative
