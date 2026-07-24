#include "../include/editor-transform-gizmo.h"

#include "../include/editor-api.h"
#include "../../../modules/presentation/include/editor_viewport_service.h"
#include "../../../modules/renderer/include/renderer.h"
#include "../../../core/sprite-draw-math.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorTransformGizmo {

namespace {

void draw_rect_outline(Modules::Renderer& renderer,
                       float x, float y, float w, float h,
                       const Vec4& color) {
    const float t = 2.f;
    renderer.drawRect(x,         y,         w, t, color);
    renderer.drawRect(x,         y + h - t, w, t, color);
    renderer.drawRect(x,         y,         t, h, color);
    renderer.drawRect(x + w - t, y,         t, h, color);
}

void draw_resize_handle(Modules::Renderer& renderer,
                        float x,
                        float y,
                        float size) {
    const float half = size * 0.5f;
    const float left = x - half;
    const float top = y - half;
    renderer.drawRect(left, top, size, size, Vec4{1.f, 1.f, 1.f, 1.f});
    draw_rect_outline(renderer, left, top, size, size, Vec4{0.1f, 0.1f, 0.1f, 1.f});
}

bool point_in_handle(float worldX,
                     float worldY,
                     float handleX,
                     float handleY,
                     float handleSizeWorld) {
    const float half = handleSizeWorld * 0.5f;
    return worldX >= handleX - half && worldX <= handleX + half
        && worldY >= handleY - half && worldY <= handleY + half;
}

Vec2 opposite_corner(ResizeHandle handle, const EntityVisualBounds& bounds) {
    const float left = bounds.x;
    const float top = bounds.y;
    const float right = bounds.x + bounds.w;
    const float bottom = bounds.y + bounds.h;
    switch (handle) {
    case ResizeHandle::TopLeft:     return { right, bottom };
    case ResizeHandle::TopRight:    return { left, bottom };
    case ResizeHandle::BottomLeft:  return { right, top };
    case ResizeHandle::BottomRight: return { left, top };
    case ResizeHandle::None:        break;
    }
    return { left, top };
}

Vec2 top_left_from_fixed_corner(ResizeHandle handle,
                                Vec2 fixedCorner,
                                Vec2 actualSize) {
    switch (handle) {
    case ResizeHandle::TopLeft:
        return { fixedCorner.x - actualSize.x, fixedCorner.y - actualSize.y };
    case ResizeHandle::TopRight:
        return { fixedCorner.x, fixedCorner.y - actualSize.y };
    case ResizeHandle::BottomLeft:
        return { fixedCorner.x - actualSize.x, fixedCorner.y };
    case ResizeHandle::BottomRight:
        return { fixedCorner.x, fixedCorner.y };
    case ResizeHandle::None:
        break;
    }
    return fixedCorner;
}

Vec2 position_from_top_left(Vec2 topLeft, Vec2 pivot, Vec2 actualSize) {
    const Vec2 anchor = SpriteDrawMath::clampPivot(pivot);
    return {
        topLeft.x + actualSize.x * anchor.x,
        topLeft.y + actualSize.y * anchor.y,
    };
}

float preserve_sign(float original, float magnitude) {
    return (original < 0.f ? -1.f : 1.f) * std::abs(magnitude);
}

float quantize_scale(float value, float step, float minimum) {
    const float safeStep = std::max(step, 1.f);
    const float quantized = std::round(value / safeStep) * safeStep;
    return std::max(minimum, quantized);
}

void snap_point_to_grid(Vec2& point, bool snapToGrid, float gridSize) {
    if (!snapToGrid || gridSize <= 0.f) return;
    point.x = std::round(point.x / gridSize) * gridSize;
    point.y = std::round(point.y / gridSize) * gridSize;
}

} // namespace

EntityVisualBounds entity_visual_bounds(
    Modules::Renderer& renderer,
    const Transform& transform,
    const SpriteComponent& sprite,
    const std::optional<Vec2>& visualSize) {
    const Vec2 size = visualSize.value_or(
        renderer.spriteDestinationSize(sprite.spriteAssetId, transform.scale));
    const Vec2 topLeft = SpriteDrawMath::placeholderTopLeft(
        transform.position, sprite.pivot, size.x, size.y);
    return { topLeft.x, topLeft.y, size.x, size.y };
}

float resize_handle_world_size(Modules::Renderer& renderer, float screenPx) {
    (void)renderer;
    if (!EditorAPI::s_viewport) return screenPx;
    const auto& camera = EditorAPI::s_viewport->host().editorCamera;
    float zoom = static_cast<float>(camera.zoom > 0. ? camera.zoom : 1.);
    if (!(zoom > 1e-6f)) zoom = 1.f;
    return screenPx / zoom;
}

void draw_resize_handles(Modules::Renderer& renderer,
                         const EntityVisualBounds& bounds,
                         float handleSizeWorld) {
    if (handleSizeWorld <= 0.f) return;
    const float left = bounds.x;
    const float top = bounds.y;
    const float right = bounds.x + bounds.w;
    const float bottom = bounds.y + bounds.h;

    draw_resize_handle(renderer, left,   top,    handleSizeWorld);
    draw_resize_handle(renderer, right,  top,    handleSizeWorld);
    draw_resize_handle(renderer, left,   bottom, handleSizeWorld);
    draw_resize_handle(renderer, right,  bottom, handleSizeWorld);
}

ResizeHandle hit_test_resize_handle(
    float worldX,
    float worldY,
    const EntityVisualBounds& bounds,
    float handleSizeWorld) {
    if (handleSizeWorld <= 0.f) return ResizeHandle::None;

    const float left = bounds.x;
    const float top = bounds.y;
    const float right = bounds.x + bounds.w;
    const float bottom = bounds.y + bounds.h;

    const struct Candidate { ResizeHandle handle; float x; float y; } candidates[] = {
        { ResizeHandle::TopRight,    right,  top    },
        { ResizeHandle::BottomRight, right,  bottom },
        { ResizeHandle::BottomLeft,  left,   bottom },
        { ResizeHandle::TopLeft,     left,   top    },
    };

    for (const Candidate& candidate : candidates) {
        if (point_in_handle(worldX, worldY, candidate.x, candidate.y, handleSizeWorld))
            return candidate.handle;
    }
    return ResizeHandle::None;
}

Transform calculate_resize_transform(
    const ResizeDragState& drag,
    float pointerWorldX,
    float pointerWorldY,
    const TransformConstraints& constraints) {
    if (drag.handle == ResizeHandle::None
        || drag.baseVisualSize.x <= 1e-6f
        || drag.baseVisualSize.y <= 1e-6f) {
        return drag.startTransform;
    }

    Vec2 movingCorner{ pointerWorldX, pointerWorldY };
    snap_point_to_grid(movingCorner, constraints.snapToGrid, constraints.gridSize);

    const Vec2 fixedCorner = opposite_corner(drag.handle, drag.startBounds);
    const Vec2 requestedSize{
        std::max(1e-6f, std::abs(movingCorner.x - fixedCorner.x)),
        std::max(1e-6f, std::abs(movingCorner.y - fixedCorner.y)),
    };

    const float rawScaleX = requestedSize.x / drag.baseVisualSize.x;
    const float rawScaleY = requestedSize.y / drag.baseVisualSize.y;

    const float scaleX = quantize_scale(
        rawScaleX, constraints.scaleStep, constraints.minAbsScale);
    const float scaleY = quantize_scale(
        rawScaleY, constraints.scaleStep, constraints.minAbsScale);

    const Vec2 actualSize{
        drag.baseVisualSize.x * scaleX,
        drag.baseVisualSize.y * scaleY,
    };

    const Vec2 topLeft = top_left_from_fixed_corner(
        drag.handle, fixedCorner, actualSize);

    Transform result = drag.startTransform;
    result.scale.x = preserve_sign(drag.startTransform.scale.x, scaleX);
    result.scale.y = preserve_sign(drag.startTransform.scale.y, scaleY);
    result.position = position_from_top_left(topLeft, drag.pivot, actualSize);
    return result;
}

} // namespace ArtCade::EditorTransformGizmo
