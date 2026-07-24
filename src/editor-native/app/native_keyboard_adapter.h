#pragma once

#include "editor-native/app/shortcuts/shortcut_types.h"

namespace ArtCade::EditorNative {

// Single Raylib keyboard acquisition per frame. Call before pumpRmlInput.
KeyboardFrameSnapshot captureKeyboardFrame();

} // namespace ArtCade::EditorNative
