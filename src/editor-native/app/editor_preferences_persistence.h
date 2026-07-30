#pragma once

#include "editor-native/app/editor_preferences.h"

#include <string>

namespace ArtCade::EditorNative {

struct EditorPreferencesPersistResult {
    bool ok = false;
    std::string message;
};

// Best-effort load into @p prefs. Missing file → default prefs + ok.
// Malformed JSON → reset to default, ok=false with message (caller may warn).
EditorPreferencesPersistResult loadEditorPreferences(EditorPreferences& prefs);

// Atomic temp+rename write. Failure does not mutate @p prefs.
EditorPreferencesPersistResult saveEditorPreferences(const EditorPreferences& prefs);

} // namespace ArtCade::EditorNative
