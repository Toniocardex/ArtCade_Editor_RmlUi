#pragma once

#include "core/types.h"

#include <optional>

namespace ArtCade::EditorNative {

enum class ColliderAnchorX { Left, Center, Right };
enum class ColliderAnchorY { Top, Middle, Bottom };

struct ColliderOriginEdit {
    Vec2 position{};
    Vec2 offset{};
};

/** ADR-0058 pure authoring helper. Rotation is deliberately excluded because
 *  BoxCollider2D's established editor/runtime contract is axis-aligned. */
std::optional<ColliderOriginEdit> originEditForColliderAnchor(
    const Transform& transform,
    const BoxCollider2DComponent& effectiveCollider,
    ColliderAnchorX anchorX,
    ColliderAnchorY anchorY);

} // namespace ArtCade::EditorNative
