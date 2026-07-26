#pragma once

#include "editor-native/model/text_layout_math.h"
#include "editor-native/view/canvas_font.h"

namespace ArtCade::EditorNative {

/** View-level measure using CanvasFont. Same anchor math as the estimate. */
TextVisualLayout layoutSceneFrameText(const SceneFrameText& text,
                                      const CanvasFont& font);

} // namespace ArtCade::EditorNative
