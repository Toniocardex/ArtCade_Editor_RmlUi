#pragma once

#ifdef __EMSCRIPTEN__

namespace ArtCade {

/** Dedicated SpriteAnimator instance for Spritesheet Studio (not a scene entity id). */
constexpr uint32_t kSpritesheetPreviewEntityId = 0xE0000001u;

class EditorAPI;

} // namespace ArtCade

#endif
