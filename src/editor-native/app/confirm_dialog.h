#pragma once

#include "editor-native/app/unsaved_guard.h"

namespace ArtCade::EditorNative {

// Modal native prompt shown before a destructive action when the project has
// unsaved changes. Returns Save / Discard / Cancel. Platform glue only — no
// project, coordinator or renderer knowledge.
UnsavedChoice confirmUnsavedChanges();

} // namespace ArtCade::EditorNative
