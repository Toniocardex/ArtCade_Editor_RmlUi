#pragma once

#include <string>

namespace ArtCade::TextAnchorMath {

/**
 * Single source of truth for the Text component's 3x3 anchor grid
 * ("top-left".."bottom-right", legacy "left"/"center"/"right").
 *
 * The anchor names the SIDE OF THE ANCHOR POINT the text appears on, not the
 * edge of the text that touches the point: "left" means the text sits to the
 * left of the point (the text's own right edge touches the point); "top"
 * means the text sits above the point (its own bottom edge touches the
 * point). This matches how a HUD element anchored to a screen corner is
 * expected to hug that corner and grow inward.
 *
 * Output codes match Renderer::drawText's align/valign contract (see
 * renderer.h): 0 = the text's own left/top edge is at the point (no shift),
 * 1 = centered on the point, 2 = the text's own right/bottom edge is at the
 * point (shifted by the full width/height) — so "left"/"top" map to 2, and
 * "right"/"bottom" map to 0.
 */
inline void anchorCodes(const std::string& align, int& hAlign, int& vAlign) {
    if (align.find("left") != std::string::npos)        hAlign = 2;
    else if (align.find("right") != std::string::npos)  hAlign = 0;
    else                                                 hAlign = 1;

    const bool isNewAnchor = align.find('-') != std::string::npos || align == "center";
    if (align.find("top") != std::string::npos)          vAlign = 2;
    else if (align.find("bottom") != std::string::npos)  vAlign = 0;
    else if (isNewAnchor)                                vAlign = 1;
    else                                                  vAlign = 0;
}

/** Same mapping as anchorCodes(), as 0/0.5/1 fractions of width/height. */
inline void anchorFractions(const std::string& align, float& hFraction, float& vFraction) {
    int hCode = 0, vCode = 0;
    anchorCodes(align, hCode, vCode);
    hFraction = static_cast<float>(hCode) * 0.5f;
    vFraction = static_cast<float>(vCode) * 0.5f;
}

// TRACKED DEBT: this header unifies *which side gets shifted*, but the two
// callers still each measure text width/height and issue the draw call
// themselves, through two different font systems:
//   - Editor Scene View (editor-native/model/text_layout_math.cpp +
//     editor-native/view/canvas_font.cpp): a single fixed Inter atlas
//     (CanvasFont), used for every raylib-drawn canvas overlay, not just Text
//     components — an edit-time approximation, not the component's own
//     configured font.
//   - Runtime/Play (this module's renderer_draw.cpp, draw_text_command):
//     the Text component's actual `fontPath` asset via the Renderer's font
//     cache, falling back to raylib's built-in font.
// A real single measure-and-draw path would mean the editor's Scene View
// drawing through the same Renderer::drawText the runtime uses (it already
// tolerates a null Font), rather than its own CanvasFont pipeline — a
// bigger change than this anchor fix (it would make Edit-mode preview show
// each Text component's actual configured font instead of the editor's
// fixed chrome font) and deserves its own review before taking on, not a
// silent side effect of unifying the anchor math.
} // namespace ArtCade::TextAnchorMath
