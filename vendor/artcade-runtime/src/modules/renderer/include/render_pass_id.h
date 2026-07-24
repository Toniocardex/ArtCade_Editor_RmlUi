#pragma once

#include <cstdint>

namespace ArtCade::Modules {

/** Explicit render passes (ADR Phase 7). */
enum class RenderPassId : uint8_t {
    GameView = 0,
    SceneBackdrop,
    Grid,
    SceneEntities,
    Gizmo,
    Debug,
    Blit,
};

} // namespace ArtCade::Modules
