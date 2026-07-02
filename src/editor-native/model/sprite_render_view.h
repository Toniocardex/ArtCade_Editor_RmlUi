#pragma once

#include "core/types.h"

namespace ArtCade::EditorNative {

class ProjectDocument;

// Where a resolved component value came from. Instance overrides win over the
// object-type definition; absence of both means the component is not present.
enum class ComponentOrigin {
    None,
    EntityDefinition,   // inherited from the object type (EntityDef.sprite)
    InstanceOverride,   // per-instance override (SceneInstanceDef.spriteRenderer)
};

// =============================================================================
// SpriteRenderView — the immutable per-instance sprite descriptor the viewport
// and Inspector consume, so neither searches the document for components during
// draw (prompt §13). A value (not a pointer): the inherited source is an
// EntityDef SpriteComponent, a different type from the instance override, so the
// two are unified into one resolved value with an explicit origin.
// =============================================================================
struct SpriteRenderView {
    bool            present = false;   // does the entity resolve to a sprite?
    bool            visible = false;   // ...and is it visible?
    AssetId         assetId;           // referenced image asset ("" = none)
    AssetId         animationAssetId;  // referenced animation asset ("" = static)
    AnimationFrameRect sourceRect{};
    bool            hasSourceRect = false;
    ComponentOrigin origin  = ComponentOrigin::None;
};

// Override-only projection of a single instance (no inheritance lookup).
inline SpriteRenderView spriteRenderViewOf(const SceneInstanceDef& instance) {
    if (!instance.spriteRenderer.has_value()) return SpriteRenderView{};
    return SpriteRenderView{true,
                            instance.spriteRenderer->visible,
                            instance.spriteRenderer->imageAssetId,
                            instance.spriteRenderer->animationAssetId,
                            {},
                            false,
                            ComponentOrigin::InstanceOverride};
}

// The central resolution query (used by viewport + Inspector). Precedence:
//   instance override  -> use it
//   else object type sprite (EntityDef.sprite with an image) -> inherited
//   else                -> absent
// So a pre-existing project whose sprites live on the object type still renders.
SpriteRenderView resolveSpriteRenderer(const ProjectDocument& document,
                                       const SceneId& sceneId, EntityId entityId);

} // namespace ArtCade::EditorNative
