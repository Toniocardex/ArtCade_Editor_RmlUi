#include "editor-native/model/text_layout_math.h"

#include <algorithm>

namespace ArtCade::EditorNative {
namespace {

void anchorAlign(const std::string& align, float& h, float& v) {
    h = 0.f;
    v = 0.f;
    if (align.find("right") != std::string::npos) h = 1.f;
    else if (align.find("left") == std::string::npos) h = 0.5f;

    const bool isNewAnchor = align.find('-') != std::string::npos || align == "center";
    if (align.find("bottom") != std::string::npos) v = 1.f;
    else if (align.find("top") != std::string::npos) v = 0.f;
    else if (isNewAnchor) v = 0.5f;
    else v = 0.f;
}

} // namespace

TextVisualLayout layoutSceneFrameTextWithWidth(const SceneFrameText& text, float width) {
    const float height = static_cast<float>(std::max(1, text.size));
    float hAlign = 0.f;
    float vAlign = 0.f;
    anchorAlign(text.align, hAlign, vAlign);
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
