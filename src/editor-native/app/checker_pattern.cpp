#include "editor-native/app/checker_pattern.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

void drawTransparencyChecker(const Rectangle& area, const Rectangle& clip) {
    constexpr int kTile = 8;
    const int x0 = static_cast<int>(std::floor(std::max(area.x, clip.x)));
    const int y0 = static_cast<int>(std::floor(std::max(area.y, clip.y)));
    const int x1 = static_cast<int>(std::ceil(std::min(area.x + area.width, clip.x + clip.width)));
    const int y1 = static_cast<int>(std::ceil(std::min(area.y + area.height, clip.y + clip.height)));
    for (int y = y0; y < y1; y += kTile) {
        for (int x = x0; x < x1; x += kTile) {
            const bool dark = (((x - x0) / kTile) + ((y - y0) / kTile)) % 2 == 0;
            DrawRectangle(x, y, std::min(kTile, x1 - x), std::min(kTile, y1 - y),
                          dark ? Color{13, 13, 15, 255} : Color{22, 22, 26, 255});
        }
    }
}

} // namespace ArtCade::EditorNative
