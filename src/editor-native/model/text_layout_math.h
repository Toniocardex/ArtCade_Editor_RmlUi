#pragma once

#include "editor-native/model/scene_frame_snapshot.h"

namespace ArtCade::EditorNative {

struct TextVisualLayout {
    SceneFrameRect bounds;
    Vec2 drawPosition;
};

TextVisualLayout layoutSceneFrameTextWithWidth(const SceneFrameText& text, float width);

/** Deterministic Default Font estimate (no Raylib) — pick / focus / selection. */
TextVisualLayout estimateSceneFrameTextLayout(const SceneFrameText& text);

} // namespace ArtCade::EditorNative
