#include "editor-native/view/text_visual_layout.h"

#include <algorithm>

namespace ArtCade::EditorNative {

TextVisualLayout layoutSceneFrameText(const SceneFrameText& text,
                                      const CanvasFont& font) {
    const float width = std::max(
        1.f, measureCanvasText(font, text.displayText, static_cast<float>(text.size)));
    return layoutSceneFrameTextWithWidth(text, width);
}

} // namespace ArtCade::EditorNative
