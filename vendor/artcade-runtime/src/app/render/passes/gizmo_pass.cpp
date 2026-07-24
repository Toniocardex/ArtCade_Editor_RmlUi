#include "gizmo_pass.h"

#include "../editor-overlay-renderer.h"
#include "../sprite_frame_resolve.h"
#include "../../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../../modules/sprite-animator/include/sprite-animator.h"

#include <algorithm>
#include <optional>

namespace ArtCade::AppRenderPasses {

namespace {

using Modules::SpriteAnimator;

std::optional<Vec2> visualSizeForFrame(
    const SpriteAnimator::Frame& frame,
    const Vec2& scale)
{
    if (!AppRender::sprite_frame_has_pixels(frame)) return std::nullopt;
    return Vec2{
        static_cast<float>(frame.w) * std::abs(scale.x),
        static_cast<float>(frame.h) * std::abs(scale.y),
    };
}

} // namespace

void execute_gizmo_pass(SceneFrameContext& ctx) {
    if (!ctx.frameSnapshot || !ctx.entityGateway || !ctx.selectedEntityIds
        || !ctx.renderer)
        return;

    const EditorOverlayState& overlay = ctx.frameSnapshot->overlay;
    if (!overlay.inEditMode) return;
    auto* renderer = ctx.renderer;
    auto* gateway = ctx.entityGateway;
    auto* animator = ctx.spriteAnimator;
    const auto& selectedEntityIds = *ctx.selectedEntityIds;

    if (overlay.inEditMode) {
        ctx.entityGateway->forEachActiveHiddenInGame(
            [renderer, gateway, animator, &selectedEntityIds]
            (EntityId id, const Transform& transform, const PhysicsComponent&) {
                if (std::find(selectedEntityIds.begin(), selectedEntityIds.end(), id)
                    != selectedEntityIds.end()) return;
                SpriteComponent sprite{};
                if (!gateway->getSprite(id, sprite)) return;
                const auto draw = AppRender::sprite_frame_resolve(animator, id, sprite, true);
                EditorOverlayRenderer::drawHiddenInGameOutline(
                    *renderer, transform, sprite,
                    visualSizeForFrame(draw.frame, transform.scale));
            });
    }

    for (const EntityId selectedId : selectedEntityIds) {
        Transform transform{};
        SpriteComponent sprite{};
        if (!ctx.entityGateway->getTransform(selectedId, transform)
            || !ctx.entityGateway->getSprite(selectedId, sprite)) {
            continue;
        }
        const bool hiddenInGame =
            !ctx.entityGateway->visibleInGame(selectedId);
        CollisionBodyComponent collisionBody{};
        std::optional<CollisionBodyComponent> collisionOverlay;
        if (ctx.entityGateway->getResolvedCollisionBody(selectedId, collisionBody))
            collisionOverlay = collisionBody;
        const auto draw = AppRender::sprite_frame_resolve(
            ctx.spriteAnimator, selectedId, sprite, overlay.inEditMode);
        EditorOverlayState itemOverlay = overlay;
        itemOverlay.selectedId = selectedId;
        EditorOverlayRenderer::drawSelection(
            *ctx.renderer, transform, sprite, itemOverlay, hiddenInGame,
            visualSizeForFrame(draw.frame, transform.scale),
            collisionOverlay);
    }
}

} // namespace ArtCade::AppRenderPasses
