#include "editor-native/model/text_layout_math.h"

#include "core/text-anchor-math.h"

#include <algorithm>

namespace ArtCade::EditorNative {

// `width` is measured by this file's caller using the editor's own CanvasFont
// (see canvas_font.cpp), not the Text component's configured font — the
// Play/runtime draw path (scene_entities_pass.cpp) measures with the real
// font instead. Only *which side gets shifted* is shared (TextAnchorMath);
// the measure-and-draw step is still duplicated by design for now — see the
// TRACKED DEBT note in core/text-anchor-math.h before touching either side.
TextVisualLayout layoutSceneFrameTextWithWidth(const SceneFrameText& text, float width) {
    const float height = static_cast<float>(std::max(1, text.size));
    float hAlign = 0.f;
    float vAlign = 0.f;
    ArtCade::TextAnchorMath::anchorFractions(text.align, hAlign, vAlign);
    const Vec2 draw{
        text.anchorPosition.x - width * hAlign,
        text.anchorPosition.y - height * vAlign,
    };
    return TextVisualLayout{
        SceneFrameRect{draw.x, draw.y, width, height},
        draw,
    };
}

TextVisualLayout estimateSceneFrameTextLayout(const SceneFrameText& text) {
    const float width = std::max(
        1.f, static_cast<float>(text.displayText.size())
                 * static_cast<float>(std::max(1, text.size)) * 0.55f);
    return layoutSceneFrameTextWithWidth(text, width);
}

} // namespace ArtCade::EditorNative
