#pragma once

#include <cstdint>

namespace ArtCade::Modules {

/**
 * Declarative invalidation flags produced by scene mutations and lifecycle.
 * Render/presentation data is rebuilt each frame via FrameCoordinator; only
 * collision and entity activation require explicit invalidation.
 */
enum class SceneInvalidation : uint32_t {
    None            = 0,
    Collision       = 1u << 0,
    SceneActivation = 1u << 1,
};

constexpr SceneInvalidation operator|(
    SceneInvalidation a, SceneInvalidation b) noexcept {
    return static_cast<SceneInvalidation>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr SceneInvalidation operator&(
    SceneInvalidation a, SceneInvalidation b) noexcept {
    return static_cast<SceneInvalidation>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr SceneInvalidation& operator|=(
    SceneInvalidation& a, SceneInvalidation b) noexcept {
    a = a | b;
    return a;
}

/** @return true when @p flags contains every bit set in @p bit. */
constexpr bool scene_invalidation_has(
    SceneInvalidation flags, SceneInvalidation bit) noexcept {
    return (flags & bit) != SceneInvalidation::None;
}

constexpr bool scene_invalidation_needs_collision_rebuild(
    SceneInvalidation flags) noexcept {
    return scene_invalidation_has(flags, SceneInvalidation::Collision);
}

} // namespace ArtCade::Modules
