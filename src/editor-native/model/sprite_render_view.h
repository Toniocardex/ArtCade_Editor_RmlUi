#pragma once

#include "core/types.h"

#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

// Where a resolved component value came from. Instance overrides win over the
// object-type definition; absence of both means the component is not present.
enum class ComponentOrigin {
    None,
    EntityDefinition,   // inherited from Object Type defaults
    InstanceOverride,   // one or more values come from the sparse instance delta
};

// Ownership merge for renderer + animator (OT defaults + explicit instance deltas).
// Edit and Play both consume this so authority cannot drift between projections.
struct ResolvedSpriteOwnership {
    std::optional<SpriteRendererComponent> renderer;
    std::optional<SpriteAnimatorComponent> animator;
    ComponentOrigin rendererOrigin = ComponentOrigin::None;
    ComponentOrigin animatorOrigin = ComponentOrigin::None;
};

// Animator config after ownership resolve. Explicit override flags distinguish
// Inherited OT values from instance-authored deltas (Inspector OT path).
struct ResolvedSpriteAnimator {
    std::optional<SpriteAnimatorComponent> animator;
    ComponentOrigin origin = ComponentOrigin::None;
    bool explicitAnimationAsset = false;
    bool explicitDefaultClip = false;
    bool explicitAutoPlay = false;
    bool explicitPlaybackSpeed = false;
};

// Draw presentation: image + source rect + visibility. No tint authority on
// SpriteRendererComponent (schema v9) — do not invent a parallel tint source.
struct ResolvedSpriteDraw {
    bool present = false;
    bool visible = false;
    AssetId imageAssetId;
    AnimationFrameRect sourceRect{};
    bool hasSourceRect = false;
    // True when an animator was present but could not resolve (Edit falls back
    // to the static renderer image when available).
    bool animatorInvalid = false;
    std::string diagnosticCode;    // e.g. ANIMATION_MISSING_ASSET
    std::string diagnosticMessage;
};

// Legacy name kept for call sites: ownership merge of renderer + animator.
using ResolvedSpritePresentation = ResolvedSpriteOwnership;

ResolvedSpriteOwnership resolveSpriteOwnership(
    const EntityDef& objectType, const SceneInstanceDef& instance);

inline ResolvedSpritePresentation resolveSpritePresentation(
    const EntityDef& objectType, const SceneInstanceDef& instance) {
    return resolveSpriteOwnership(objectType, instance);
}

ResolvedSpriteAnimator resolveSpriteAnimator(
    const EntityDef& objectType, const SceneInstanceDef& instance);

// Draw resolve. runtimeFrame, when non-null, wins over the default-clip still.
// Invalid animator → animatorInvalid + static renderer fallback when possible.
ResolvedSpriteDraw resolveSpriteDraw(
    const ProjectDocument& document,
    const std::optional<SpriteRendererComponent>& renderer,
    const std::optional<SpriteAnimatorComponent>& animator,
    const AnimationFrameRect* runtimeFrame = nullptr);

// =============================================================================
// SpriteRenderView — the immutable per-instance sprite descriptor the viewport
// and Inspector consume, so neither searches the document for components during
// draw (prompt §13).
// =============================================================================
struct SpriteRenderView {
    bool            present = false;
    bool            visible = false;
    AssetId         assetId;
    AssetId         animationAssetId;
    AnimationFrameRect sourceRect{};
    bool            hasSourceRect = false;
    ComponentOrigin origin  = ComponentOrigin::None;
    bool            animatorInvalid = false;
    std::string     diagnosticCode;
    std::string     diagnosticMessage;
};

SpriteRenderView resolveSpriteRenderer(const ProjectDocument& document,
                                       const SceneId& sceneId, EntityId entityId);

// Live authoring diagnostics for animation integrity (Problems / Inspector).
struct AnimationAuthoringDiagnostic {
    std::string code;       // ANIMATION_*
    std::string message;
    ObjectTypeId objectTypeId;
    AssetId animationAssetId;
    SceneId sceneId;
    EntityId entityId = INVALID_ENTITY;
};

std::vector<AnimationAuthoringDiagnostic> collectAnimationAuthoringDiagnostics(
    const ProjectDocument& document);

} // namespace ArtCade::EditorNative
