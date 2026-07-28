#pragma once

#include "editor-native/model/text_layout_math.h"
#include "editor-native/view/canvas_font.h"

namespace ArtCade::EditorNative {

/** View-level measure using CanvasFont. Same anchor math as the estimate. */
TextVisualLayout layoutSceneFrameText(const SceneFrameText& text,
                                      const CanvasFont& font);

/** Same, measuring with an explicit resolved Font (ADR-0036: a Text
 *  component's own configured font, not the default CanvasFont). */
TextVisualLayout layoutSceneFrameText(const SceneFrameText& text, const Font& font);

} // namespace ArtCade::EditorNative
