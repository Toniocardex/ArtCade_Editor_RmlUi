#pragma once

#include "editor-native/model/scene_frame_snapshot.h"

#include <cstdint>
#include <optional>

namespace ArtCade::EditorNative {

struct SpritePixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

std::optional<SpritePixelRect> computeOpaqueSpritePixelBounds(
    const std::uint8_t* alpha,
    int imageWidth,
    int imageHeight,
    SpritePixelRect source);

// Tightens only the Edit authoring geometry. Rendering destination/origin
// remain unchanged and authored Sprite Pivot is never replaced.
void applyOpaqueSpriteGeometry(SceneFrameSprite& sprite,
                               SpritePixelRect source,
                               SpritePixelRect opaque);

} // namespace ArtCade::EditorNative
