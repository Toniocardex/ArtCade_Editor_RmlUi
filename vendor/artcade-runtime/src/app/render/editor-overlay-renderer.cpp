#include "editor-overlay-renderer.h"



#include "../../modules/editor-api/include/editor-transform-gizmo.h"

#include "../../modules/renderer/include/renderer.h"

#include "../../modules/collision/include/collision_math.h"

#include "../../modules/collision/include/collision_world.h"



#include <algorithm>

#include <cmath>



namespace ArtCade::EditorOverlayRenderer {



namespace {



void drawRectOutline(Modules::Renderer& renderer,

                     float x, float y, float w, float h,

                     const Vec4& color) {

    const float t = 2.f;
    // Editor overlays run inside an active BeginMode2D pass — draw immediately.
    renderer.drawRectImmediate(x,         y,         w, t, color);

    renderer.drawRectImmediate(x,         y + h - t, w, t, color);

    renderer.drawRectImmediate(x,         y,         t, h, color);

    renderer.drawRectImmediate(x + w - t, y,         t, h, color);

}



void drawEntityOutline(Modules::Renderer& renderer,

                       const EntityVisualBounds& bounds,

                       const Vec4& color) {

    drawRectOutline(renderer, bounds.x, bounds.y, bounds.w, bounds.h, color);

}



} // namespace



void drawBackdrop(Modules::Renderer& renderer,

                  const Vec4& backgroundColor,

                  const EditorOverlayState& state) {

    if (!state.inEditMode) return;

    const Vec2 cam     = renderer.getCameraPosition();

    const Vec2 visible = renderer.visibleWorldSize();

    renderer.drawRectImmediate(

        cam.x, cam.y,

        std::max(1.f, visible.x),

        std::max(1.f, visible.y),

        backgroundColor);

}



void drawGrid(Modules::Renderer& renderer,

              const Vec2& worldSize,

              const EditorOverlayState& state) {

    if (!state.inEditMode || !state.guidesEnabled) return;



    const float w = std::max(1.f, worldSize.x);

    const float h = std::max(1.f, worldSize.y);



    // Scene bounds — empty edit view must read as a framed workspace, not void.

    const Vec4 border{0.92f, 0.92f, 0.94f, 0.6f};

    drawRectOutline(renderer, 0.f, 0.f, w, h, border);



    const Vec4 grid{0.918f, 0.918f, 0.918f, 0.55f};

    const float step = state.gridSize > 0.f ? state.gridSize : 32.f;

    const float hair = 1.f;

    if (step >= 4.f) {

        for (float x = step; x < w; x += step)

            renderer.drawRectImmediate(x, 0.f, hair, h, grid);

        for (float y = step; y < h; y += step)

            renderer.drawRectImmediate(0.f, y, w, hair, grid);

    }

}



void drawBackdrop(Modules::Renderer& renderer,

                  const SceneDef& scene,

                  const EditorOverlayState& state) {

    drawBackdrop(renderer, scene.backgroundColor, state);

}



void drawGrid(Modules::Renderer& renderer,

              const SceneDef& scene,

              const EditorOverlayState& state) {

    drawGrid(renderer, scene.worldSize, state);

}



void drawSelection(Modules::Renderer& renderer,

                   const Transform& transform,

                   const SpriteComponent& sprite,

                   const EditorOverlayState& state,

                   bool hiddenInGame,

                   const std::optional<Vec2>& visualSize,

                   const std::optional<CollisionBodyComponent>& collisionBody) {

    if (!state.inEditMode || state.selectedId == 0u) return;



    const Vec2 p = transform.position;



    if (collisionBody && collisionBody->enabled) {

        const Vec4 collisionColor{0.22f, 0.74f, 0.98f, 0.95f};

        for (const CollisionShape& shape : collisionBody->shapes) {

            if (!shape.enabled) continue;

            const auto inst = CollisionWorld::shapeInstance(transform, shape);

            const auto aabb = PhysicsMath::shapeWorldAabb(inst);

            drawRectOutline(

                renderer,

                aabb.minX,

                aabb.minY,

                aabb.maxX - aabb.minX,

                aabb.maxY - aabb.minY,

                collisionColor);

        }

    }



    const EntityVisualBounds bounds = EditorTransformGizmo::entity_visual_bounds(

        renderer, transform, sprite, visualSize);

    const Vec4 sel = hiddenInGame

        ? Vec4{1.f, 0.55f, 0.1f, 1.f}

        : Vec4{1.f, 1.f, 0.f, 1.f};

    drawEntityOutline(renderer, bounds, sel);



    const float handleSize = EditorTransformGizmo::resize_handle_world_size(renderer);

    EditorTransformGizmo::draw_resize_handles(renderer, bounds, handleSize);

}



void drawHiddenInGameOutline(Modules::Renderer& renderer,

                             const Transform& transform,

                             const SpriteComponent& sprite,

                             const std::optional<Vec2>& visualSize) {

    const EntityVisualBounds bounds = EditorTransformGizmo::entity_visual_bounds(

        renderer, transform, sprite, visualSize);

    const Vec4 amber{1.f, 0.55f, 0.1f, 0.75f};

    drawEntityOutline(renderer, bounds, amber);

}



} // namespace ArtCade::EditorOverlayRenderer


