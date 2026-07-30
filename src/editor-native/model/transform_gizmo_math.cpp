#include "editor-native/model/transform_gizmo_math.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/view/scene_grid.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

namespace {

constexpr float kDefaultUnscaled = 32.f;
constexpr float kRotationEpsilon = 0.00001f;

bool rotationIsEffectivelyZero(float radians) {
    return std::fabs(radians) <= kRotationEpsilon;
}

bool entityEmittedInFrame(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameEntity& e : frame.entities) {
        if (e.entityId == entityId) return true;
    }
    return false;
}

Vec2 cornerWorld(const SceneFrameTransform2D& g, float sx, float sy) {
    // Local corner offsets in unrotated space (sx/sy = ±1).
    const float hx = g.size.x * 0.5f * sx;
    const float hy = g.size.y * 0.5f * sy;
    // Slice 1: scale only when rotation ≈ 0, so no local→world rotation here.
    return Vec2{g.center.x + hx, g.center.y + hy};
}

// Mid-edge positions.
Vec2 midEdgeWorld(const SceneFrameTransform2D& g, TransformHandle handle) {
    switch (handle) {
    case TransformHandle::EdgeT:
        return Vec2{g.center.x, g.center.y - g.size.y * 0.5f};
    case TransformHandle::EdgeB:
        return Vec2{g.center.x, g.center.y + g.size.y * 0.5f};
    case TransformHandle::EdgeL:
        return Vec2{g.center.x - g.size.x * 0.5f, g.center.y};
    case TransformHandle::EdgeR:
        return Vec2{g.center.x + g.size.x * 0.5f, g.center.y};
    default:
        return g.center;
    }
}

TransformHandle oppositeHandle(TransformHandle handle) {
    switch (handle) {
    case TransformHandle::CornerTL: return TransformHandle::CornerBR;
    case TransformHandle::CornerTR: return TransformHandle::CornerBL;
    case TransformHandle::CornerBR: return TransformHandle::CornerTL;
    case TransformHandle::CornerBL: return TransformHandle::CornerTR;
    case TransformHandle::EdgeT: return TransformHandle::EdgeB;
    case TransformHandle::EdgeB: return TransformHandle::EdgeT;
    case TransformHandle::EdgeL: return TransformHandle::EdgeR;
    case TransformHandle::EdgeR: return TransformHandle::EdgeL;
    default: return TransformHandle::None;
    }
}

Vec2 handleWorldPos(const SceneFrameTransform2D& g, TransformHandle handle) {
    switch (handle) {
    case TransformHandle::CornerTL: return cornerWorld(g, -1.f, -1.f);
    case TransformHandle::CornerTR: return cornerWorld(g, 1.f, -1.f);
    case TransformHandle::CornerBR: return cornerWorld(g, 1.f, 1.f);
    case TransformHandle::CornerBL: return cornerWorld(g, -1.f, 1.f);
    case TransformHandle::EdgeT:
    case TransformHandle::EdgeR:
    case TransformHandle::EdgeB:
    case TransformHandle::EdgeL:
        return midEdgeWorld(g, handle);
    default:
        return g.center;
    }
}

} // namespace

TransformGizmoCapabilities resolveTransformGizmoCapabilities(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId entityId) {
    TransformGizmoCapabilities caps;
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId, entityId);
    if (!inst) return caps;

    caps.canMove = true;

    if (inst->tilemap.has_value()) return caps; // scale no
    if (!rotationIsEffectivelyZero(inst->transform.rotation)) return caps;

    const EntityDef* type = document.findObjectType(inst->objectTypeId);
    const SpriteRenderView sprite = resolveSpriteRenderer(document, sceneId, entityId);
    const bool hasSprite = sprite.present && !sprite.assetId.empty();
    const bool hasCollider = type && type->boxCollider2D && type->boxCollider2D->enabled;
    const bool hasText = type && type->text
        && (!type->text->text.empty() || !type->text->bindKey.empty());
    const bool hasGauge = type && type->gauge
        && type->gauge->width > 0.f && type->gauge->height > 0.f;

    if (!hasSprite && !hasCollider && (hasText || hasGauge)) return caps;

    caps.canScale = true;
    return caps;
}

std::optional<InstanceTransformGeometry> resolveInstanceTransformGeometry(
    const ProjectDocument& document,
    const SceneFrameSnapshot& frame,
    const SceneId& sceneId,
    EntityId entityId) {
    if (!entityEmittedInFrame(frame, entityId)) return std::nullopt;

    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId, entityId);
    if (!inst) return std::nullopt;

    const TransformGizmoCapabilities caps =
        resolveTransformGizmoCapabilities(document, sceneId, entityId);
    if (!caps.canMove) return std::nullopt;

    InstanceTransformGeometry out;
    out.unscaledSize = Vec2{kDefaultUnscaled, kDefaultUnscaled};
    out.transform = projectTransform(inst->transform, out.unscaledSize);
    out.supportsScale = caps.canScale;
    return out;
}

std::optional<Vec2> transformHandleWorldPosition(
    const SceneFrameTransform2D& geometry,
    TransformHandle handle) {
    switch (handle) {
    case TransformHandle::CornerTL:
    case TransformHandle::CornerTR:
    case TransformHandle::CornerBR:
    case TransformHandle::CornerBL:
    case TransformHandle::EdgeT:
    case TransformHandle::EdgeR:
    case TransformHandle::EdgeB:
    case TransformHandle::EdgeL:
        return handleWorldPos(geometry, handle);
    default:
        return std::nullopt;
    }
}

TransformHandle hitTestTransformHandle(
    const SceneFrameTransform2D& geometry,
    bool canScale,
    const SceneViewCamera& camera,
    Vec2 screenMouse) {
    if (!canScale) return TransformHandle::None;

    const float halfHit = kTransformHandleHitPx * 0.5f;
    static constexpr TransformHandle kScaleHandles[] = {
        TransformHandle::CornerTL, TransformHandle::CornerTR,
        TransformHandle::CornerBR, TransformHandle::CornerBL,
        TransformHandle::EdgeT, TransformHandle::EdgeR,
        TransformHandle::EdgeB, TransformHandle::EdgeL,
    };

    TransformHandle best = TransformHandle::None;
    float bestDistSq = halfHit * halfHit;

    for (TransformHandle h : kScaleHandles) {
        const Vec2 world = handleWorldPos(geometry, h);
        const Vec2 screen = worldToScreen(camera, world);
        const float dx = screen.x - screenMouse.x;
        const float dy = screen.y - screenMouse.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestDistSq) {
            bestDistSq = d2;
            best = h;
        }
    }
    return best;
}

bool transformBodyContainsWorldPoint(
    const SceneFrameTransform2D& geometry,
    Vec2 worldPoint) {
    return transformContainsPoint(geometry, worldPoint);
}

Transform clampAuthoringScale(Transform transform) {
    transform.scale.x = std::max(transform.scale.x, kMinAuthoringScale);
    transform.scale.y = std::max(transform.scale.y, kMinAuthoringScale);
    return transform;
}

Transform moveTransformFromPointer(
    const TransformInteractionState& state,
    Vec2 currentMouseWorld) {
    Transform out = state.originalTransform;
    out.position = Vec2{
        state.originalTransform.position.x
            + (currentMouseWorld.x - state.startMouseWorld.x),
        state.originalTransform.position.y
            + (currentMouseWorld.y - state.startMouseWorld.y),
    };
    return out;
}

Transform resizeTransformFromHandle(
    const TransformInteractionState& state,
    Vec2 currentMouseWorld,
    bool preserveAspect,
    const TransformResizeSnap* snap) {
    Transform out = state.originalTransform;
    const Vec2 unscaled = state.unscaledSize;
    const float minW = unscaled.x * kMinAuthoringScale;
    const float minH = unscaled.y * kMinAuthoringScale;
    const Vec2 fixed = state.fixedAnchorWorld;
    Vec2 mouse = currentMouseWorld;
    if (snap && snap->enabled) {
        // Free handle tracks a grid point so edges land on cell lines / sizes.
        const SceneGridDefinition grid{SceneGridKind::World, snap->cellSize, snap->origin};
        mouse = snapWorldPositionToGrid(mouse, grid);
    }

    const auto snapExtent = [](float length, float cell, float minLen) {
        if (!(cell > 0.f) || !std::isfinite(cell)) return std::max(length, minLen);
        float snapped = std::round(length / cell) * cell;
        if (snapped < minLen) {
            snapped = std::ceil(minLen / cell - 1e-5f) * cell;
        }
        return std::max(snapped, minLen);
    };

    const auto snapFreeEdges = [&](float& left, float& top, float& right, float& bottom) {
        if (!snap || !snap->enabled) return;
        const float cx = snap->cellSize.x;
        const float cy = snap->cellSize.y;
        switch (state.handle) {
        case TransformHandle::CornerBR:
            right = left + snapExtent(right - left, cx, minW);
            bottom = top + snapExtent(bottom - top, cy, minH);
            break;
        case TransformHandle::CornerBL:
            left = right - snapExtent(right - left, cx, minW);
            bottom = top + snapExtent(bottom - top, cy, minH);
            break;
        case TransformHandle::CornerTR:
            right = left + snapExtent(right - left, cx, minW);
            top = bottom - snapExtent(bottom - top, cy, minH);
            break;
        case TransformHandle::CornerTL:
            left = right - snapExtent(right - left, cx, minW);
            top = bottom - snapExtent(bottom - top, cy, minH);
            break;
        case TransformHandle::EdgeR:
            right = left + snapExtent(right - left, cx, minW);
            break;
        case TransformHandle::EdgeL:
            left = right - snapExtent(right - left, cx, minW);
            break;
        case TransformHandle::EdgeB:
            bottom = top + snapExtent(bottom - top, cy, minH);
            break;
        case TransformHandle::EdgeT:
            top = bottom - snapExtent(bottom - top, cy, minH);
            break;
        default:
            break;
        }
    };

    const auto applyBox = [&](float left, float top, float right, float bottom) {
        float w = right - left;
        float h = bottom - top;
        if (w < minW) {
            if (std::fabs(left - fixed.x) < std::fabs(right - fixed.x)) {
                left = right - minW;
            } else {
                right = left + minW;
            }
            w = minW;
        }
        if (h < minH) {
            if (std::fabs(top - fixed.y) < std::fabs(bottom - fixed.y)) {
                top = bottom - minH;
            } else {
                bottom = top + minH;
            }
            h = minH;
        }

        Vec2 scale{w / unscaled.x, h / unscaled.y};
        if (preserveAspect) {
            const float ox = std::max(state.originalTransform.scale.x, kMinAuthoringScale);
            const float oy = std::max(state.originalTransform.scale.y, kMinAuthoringScale);
            const float fx = scale.x / ox;
            const float fy = scale.y / oy;
            const float uniform = (std::fabs(fx - 1.f) >= std::fabs(fy - 1.f)) ? fx : fy;
            scale = Vec2{ox * uniform, oy * uniform};
            scale.x = std::max(scale.x, kMinAuthoringScale);
            scale.y = std::max(scale.y, kMinAuthoringScale);
            w = unscaled.x * scale.x;
            h = unscaled.y * scale.y;
            switch (state.handle) {
            case TransformHandle::CornerBR:
                left = fixed.x;
                top = fixed.y;
                right = left + w;
                bottom = top + h;
                break;
            case TransformHandle::CornerBL:
                right = fixed.x;
                top = fixed.y;
                left = right - w;
                bottom = top + h;
                break;
            case TransformHandle::CornerTR:
                left = fixed.x;
                bottom = fixed.y;
                right = left + w;
                top = bottom - h;
                break;
            case TransformHandle::CornerTL:
                right = fixed.x;
                bottom = fixed.y;
                left = right - w;
                top = bottom - h;
                break;
            case TransformHandle::EdgeR:
                left = fixed.x;
                right = left + w;
                top = fixed.y - h * 0.5f;
                bottom = fixed.y + h * 0.5f;
                break;
            case TransformHandle::EdgeL:
                right = fixed.x;
                left = right - w;
                top = fixed.y - h * 0.5f;
                bottom = fixed.y + h * 0.5f;
                break;
            case TransformHandle::EdgeB:
                top = fixed.y;
                bottom = top + h;
                left = fixed.x - w * 0.5f;
                right = fixed.x + w * 0.5f;
                break;
            case TransformHandle::EdgeT:
                bottom = fixed.y;
                top = bottom - h;
                left = fixed.x - w * 0.5f;
                right = fixed.x + w * 0.5f;
                break;
            default:
                break;
            }
        }

        snapFreeEdges(left, top, right, bottom);
        w = right - left;
        h = bottom - top;
        scale = Vec2{w / unscaled.x, h / unscaled.y};

        out.scale = scale;
        out.position = Vec2{(left + right) * 0.5f, (top + bottom) * 0.5f};
    };

    const SceneFrameTransform2D originalGeom =
        projectTransform(state.originalTransform, unscaled);
    const float origLeft = originalGeom.center.x - originalGeom.size.x * 0.5f;
    const float origTop = originalGeom.center.y - originalGeom.size.y * 0.5f;
    const float origRight = originalGeom.center.x + originalGeom.size.x * 0.5f;
    const float origBottom = originalGeom.center.y + originalGeom.size.y * 0.5f;

    switch (state.handle) {
    case TransformHandle::CornerBR:
        applyBox(fixed.x, fixed.y,
                 std::max(mouse.x, fixed.x + minW),
                 std::max(mouse.y, fixed.y + minH));
        break;
    case TransformHandle::CornerBL:
        applyBox(std::min(mouse.x, fixed.x - minW), fixed.y,
                 fixed.x,
                 std::max(mouse.y, fixed.y + minH));
        break;
    case TransformHandle::CornerTR:
        applyBox(fixed.x, std::min(mouse.y, fixed.y - minH),
                 std::max(mouse.x, fixed.x + minW),
                 fixed.y);
        break;
    case TransformHandle::CornerTL:
        applyBox(std::min(mouse.x, fixed.x - minW),
                 std::min(mouse.y, fixed.y - minH),
                 fixed.x, fixed.y);
        break;
    case TransformHandle::EdgeR:
        applyBox(fixed.x, origTop,
                 std::max(mouse.x, fixed.x + minW),
                 origBottom);
        break;
    case TransformHandle::EdgeL:
        applyBox(std::min(mouse.x, fixed.x - minW), origTop,
                 fixed.x, origBottom);
        break;
    case TransformHandle::EdgeB:
        applyBox(origLeft, fixed.y,
                 origRight,
                 std::max(mouse.y, fixed.y + minH));
        break;
    case TransformHandle::EdgeT:
        applyBox(origLeft,
                 std::min(mouse.y, fixed.y - minH),
                 origRight, fixed.y);
        break;
    default:
        break;
    }

    return clampAuthoringScale(out);
}

TransformInteractionState beginTransformInteraction(
    const SceneId& sceneId,
    EntityId entityId,
    TransformHandle handle,
    const Transform& authored,
    const InstanceTransformGeometry& geometry,
    Vec2 mouseWorld) {
    TransformInteractionState state;
    state.active = true;
    state.sceneId = sceneId;
    state.entityId = entityId;
    state.handle = handle;
    state.originalTransform = authored;
    state.previewTransform = authored;
    state.startMouseWorld = mouseWorld;
    state.unscaledSize = geometry.unscaledSize;

    if (handle != TransformHandle::Body && handle != TransformHandle::None) {
        const TransformHandle opp = oppositeHandle(handle);
        state.fixedAnchorWorld = handleWorldPos(geometry.transform, opp);
        // For edges, fixed anchor is the opposite edge midpoint (already).
        // For EdgeL/R also keep vertical centre via midpoint; for EdgeT/B
        // horizontal centre — handleWorldPos already returns midpoints.
    }
    return state;
}

AuthoredTransformPatch transformPatchForRelease(const TransformInteractionState& state) {
    AuthoredTransformPatch patch;
    if (!state.active) return patch;

    const Transform& o = state.originalTransform;
    const Transform& p = state.previewTransform;

    if (state.handle == TransformHandle::Body) {
        if (!nearlyEqualTransform(o.position, p.position)) {
            patch.position = p.position;
        }
        return patch;
    }

    // Resize: always position + scale when either changed.
    const bool posChanged = !nearlyEqualTransform(o.position, p.position);
    const bool scaleChanged = !nearlyEqualTransform(o.scale, p.scale);
    if (posChanged || scaleChanged) {
        patch.position = p.position;
        patch.scale = p.scale;
    }
    return patch;
}

} // namespace ArtCade::EditorNative
