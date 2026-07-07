#include "editor-native/model/scene_frame_snapshot.h"

#include "editor-native/model/play_session.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tilemap_render_view.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

constexpr float kDefaultSpriteExtent = 48.f;

const Vec3* fillFor(const ProjectDocument& document, const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    return it == types.end() ? nullptr : &it->second.sprite.fillColor;
}

SceneFrameRect instanceBounds(const SceneInstanceDef& inst) {
    const Transform& transform = inst.transform;
    const Vec2 pos = transform.position;
    const Vec2 scl = transform.scale;
    const float width = kDefaultSpriteExtent * (scl.x == 0.f ? 1.f : scl.x);
    const float height = kDefaultSpriteExtent * (scl.y == 0.f ? 1.f : scl.y);
    return SceneFrameRect{pos.x - width * 0.5f, pos.y - height * 0.5f, width, height};
}

SceneFrameRect transformBounds(const Transform& transform) {
    const Vec2 pos = transform.position;
    const Vec2 scl = transform.scale;
    const float width = kDefaultSpriteExtent * (scl.x == 0.f ? 1.f : scl.x);
    const float height = kDefaultSpriteExtent * (scl.y == 0.f ? 1.f : scl.y);
    return SceneFrameRect{pos.x - width * 0.5f, pos.y - height * 0.5f, width, height};
}

WorldRect toWorldRect(const SceneFrameRect& rect) {
    return WorldRect{rect.x, rect.y, rect.width, rect.height};
}

float left(const WorldRect& rect) { return rect.x; }
float top(const WorldRect& rect) { return rect.y; }
float right(const WorldRect& rect) { return rect.x + rect.width; }
float bottom(const WorldRect& rect) { return rect.y + rect.height; }

bool finiteRect(const WorldRect& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y)
        && std::isfinite(rect.width) && std::isfinite(rect.height)
        && rect.width >= 0.f && rect.height >= 0.f;
}

bool finiteScene(Vec2 sceneSize) {
    return std::isfinite(sceneSize.x) && std::isfinite(sceneSize.y)
        && sceneSize.x > 0.f && sceneSize.y > 0.f;
}

WorldRect unite(const WorldRect& a, const WorldRect& b) {
    const float x0 = std::min(left(a), left(b));
    const float y0 = std::min(top(a), top(b));
    const float x1 = std::max(right(a), right(b));
    const float y1 = std::max(bottom(a), bottom(b));
    return WorldRect{x0, y0, x1 - x0, y1 - y0};
}

} // namespace

SceneFrameSnapshot collectSceneFrameSnapshot(const ProjectDocument& document,
                                             const SceneId& sceneId,
                                             EntityId selectedEntity,
                                             const std::unordered_set<std::string>& hiddenLayers) {
    SceneFrameSnapshot snapshot;
    snapshot.sceneId = sceneId;

    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) return snapshot;

    snapshot.hasScene = true;
    snapshot.sceneName = scene->name;
    snapshot.worldSize = scene->worldSize;
    snapshot.backgroundColor = scene->backgroundColor;

    std::unordered_set<EntityId> visible;   // for filtering the collider overlay
    const auto emit = [&](const SceneInstanceDef& inst) {
        const SceneFrameRect bounds = instanceBounds(inst);
        const Vec3* fill = fillFor(document, inst.objectTypeId);
        const bool selected = inst.id == selectedEntity;
        snapshot.entities.push_back(SceneFrameEntity{
            inst.id, inst.instanceName,
            fill ? *fill : Vec3{0.47f, 0.49f, 0.52f}, bounds, selected});
        const SpriteRenderView sprite = resolveSpriteRenderer(document, sceneId, inst.id);
        if (sprite.present && !sprite.assetId.empty()) {
            snapshot.sprites.push_back(SceneFrameSprite{
                inst.id, sprite.assetId, bounds,
                Vec2{bounds.width * 0.5f, bounds.height * 0.5f}, sprite.visible, selected,
                SceneFrameRect{sprite.sourceRect.x, sprite.sourceRect.y,
                               sprite.sourceRect.w, sprite.sourceRect.h},
                sprite.hasSourceRect});
        }
        if (inst.tilemap.has_value()) {
            // A dangling tilesetAssetId shouldn't survive validation, but if one
            // reaches here anyway the tilemap simply isn't emitted, rather than
            // emitted with a blank image id the renderer would need to guard.
            if (const TilesetAsset* tileset =
                    document.findTilesetAsset(inst.tilemap->tilesetAssetId)) {
                snapshot.tilemaps.push_back(SceneFrameTilemap{
                    inst.id, tileset->imageAssetId,
                    tilemapRenderCells(*inst.tilemap, *tileset, inst.transform.position),
                    selected});
            }
        }
        visible.insert(inst.id);
    };

    // An instance's effective layer, needed only to filter hidden layers here
    // (orderedInstances already applies the same rule for ordering).
    const auto effectiveLayer = [&](const SceneInstanceDef& inst) -> std::string {
        if (!inst.layerId.empty() && document.hasLayer(sceneId, inst.layerId)) return inst.layerId;
        return scene->defaultLayerId;
    };
    for (const SceneInstanceDef* inst : document.orderedInstances(sceneId)) {
        if (!hiddenLayers.empty() && hiddenLayers.count(effectiveLayer(*inst))) continue;
        emit(*inst);
    }

    // Collider overlays follow the same visibility as their entities.
    snapshot.colliders = collectBoxColliderBounds(document, sceneId, selectedEntity);
    snapshot.colliders.erase(
        std::remove_if(snapshot.colliders.begin(), snapshot.colliders.end(),
                       [&](const SceneFrameCollider& c) { return visible.count(c.entityId) == 0; }),
        snapshot.colliders.end());

    return snapshot;
}

SceneFrameSnapshot collectSceneFrameSnapshot(const PlaySession& session) {
    SceneFrameSnapshot snapshot;
    const RuntimeScene& scene = session.scene();
    snapshot.sceneId = scene.sourceSceneId;
    snapshot.hasScene = true;
    snapshot.sceneName = scene.name;
    snapshot.worldSize = scene.worldSize;
    snapshot.backgroundColor = scene.backgroundColor;

    for (const RuntimeEntity& entity : scene.entities) {
        const SceneFrameRect bounds = transformBounds(entity.transform);
        // A tilemap entity never falls back to the generic editor placeholder
        // in Play, painted or not - unlike Edit, where the placeholder is a
        // deliberate authoring affordance (see tilemap_render_view.h / Slice 5).
        if (!entity.tilemap.has_value()) {
            snapshot.entities.push_back(SceneFrameEntity{
                entity.id,
                entity.name,
                entity.fillColor,
                bounds,
                false,
            });
        }

        if (entity.tilemap.has_value() && !entity.tilemap->cells.empty()) {
            snapshot.tilemaps.push_back(SceneFrameTilemap{
                entity.id, entity.tilemap->imageAssetId, entity.tilemap->cells, false});
        }

        if (entity.sprite.has_value() && !entity.sprite->assetId.empty()) {
            snapshot.sprites.push_back(SceneFrameSprite{
                entity.id,
                entity.sprite->assetId,
                bounds,
                Vec2{bounds.width * 0.5f, bounds.height * 0.5f},
                entity.sprite->visible,
                false,
                SceneFrameRect{entity.sprite->sourceRect.x, entity.sprite->sourceRect.y,
                               entity.sprite->sourceRect.w, entity.sprite->sourceRect.h},
                entity.sprite->hasSourceRect,
            });
        }
    }

    return snapshot;
}

namespace {

bool rectContains(const SceneFrameRect& r, Vec2 p) {
    return p.x >= r.x && p.x <= r.x + r.width
        && p.y >= r.y && p.y <= r.y + r.height;
}

} // namespace

EntityId pickEntityAt(const SceneFrameSnapshot& frame, Vec2 worldPoint) {
    // Placeholders draw first, then tilemap cells, then sprites; the last
    // drawn item that contains the point is on top, so let later hits
    // override earlier ones (matches SceneView::render's pass order).
    EntityId hit = INVALID_ENTITY;
    for (const SceneFrameEntity& entity : frame.entities)
        if (rectContains(entity.bounds, worldPoint)) hit = entity.entityId;
    for (const SceneFrameTilemap& tilemap : frame.tilemaps)
        for (const SceneFrameTilemapCell& cell : tilemap.cells)
            if (rectContains(cell.destination, worldPoint)) hit = tilemap.entityId;
    for (const SceneFrameSprite& sprite : frame.sprites)
        if (sprite.visible && rectContains(sprite.destination, worldPoint)) hit = sprite.entityId;
    return hit;
}

std::optional<WorldRect> editorBoundsForEntity(const SceneFrameSnapshot& frame,
                                               EntityId entityId) {
    std::optional<WorldRect> bounds;
    for (const SceneFrameSprite& sprite : frame.sprites) {
        if (sprite.entityId != entityId || !sprite.visible || sprite.assetId.empty()) continue;
        const WorldRect rect = toWorldRect(sprite.destination);
        if (!finiteRect(rect)) continue;
        bounds = bounds ? unite(*bounds, rect) : rect;
    }
    for (const SceneFrameCollider& collider : frame.colliders) {
        if (collider.entityId != entityId) continue;
        if (!finiteRect(collider.worldBounds)) continue;
        bounds = bounds ? unite(*bounds, collider.worldBounds) : collider.worldBounds;
    }
    for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
        if (tilemap.entityId != entityId) continue;
        for (const SceneFrameTilemapCell& cell : tilemap.cells) {
            const WorldRect rect = toWorldRect(cell.destination);
            if (!finiteRect(rect)) continue;
            bounds = bounds ? unite(*bounds, rect) : rect;
        }
    }
    if (bounds) return bounds;

    for (const SceneFrameEntity& entity : frame.entities) {
        if (entity.entityId != entityId) continue;
        const WorldRect rect = toWorldRect(entity.bounds);
        if (finiteRect(rect)) return rect;
        break;
    }
    return std::nullopt;
}

SceneContainment classifySceneContainment(const WorldRect& entityBounds,
                                          Vec2 sceneSize) {
    if (!finiteRect(entityBounds) || !finiteScene(sceneSize)) {
        return SceneContainment::FullyOutside;
    }

    const WorldRect scene{0.f, 0.f, sceneSize.x, sceneSize.y};
    const bool intersects =
        right(entityBounds) > left(scene)
        && left(entityBounds) < right(scene)
        && bottom(entityBounds) > top(scene)
        && top(entityBounds) < bottom(scene);
    if (!intersects) return SceneContainment::FullyOutside;

    const bool contained =
        left(entityBounds) >= left(scene)
        && right(entityBounds) <= right(scene)
        && top(entityBounds) >= top(scene)
        && bottom(entityBounds) <= bottom(scene);
    return contained ? SceneContainment::Inside : SceneContainment::PartiallyOutside;
}

std::optional<Vec2> positionToBringBoundsInsideScene(const WorldRect& entityBounds,
                                                     Vec2 currentPosition,
                                                     Vec2 sceneSize) {
    if (!finiteRect(entityBounds) || !finiteScene(sceneSize)
        || !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y)) {
        return std::nullopt;
    }

    const WorldRect scene{0.f, 0.f, sceneSize.x, sceneSize.y};
    Vec2 correction{0.f, 0.f};

    if (entityBounds.width <= scene.width) {
        if (left(entityBounds) < left(scene)) correction.x = left(scene) - left(entityBounds);
        else if (right(entityBounds) > right(scene)) correction.x = right(scene) - right(entityBounds);
    } else {
        correction.x = (left(scene) + scene.width * 0.5f)
                     - (left(entityBounds) + entityBounds.width * 0.5f);
    }

    if (entityBounds.height <= scene.height) {
        if (top(entityBounds) < top(scene)) correction.y = top(scene) - top(entityBounds);
        else if (bottom(entityBounds) > bottom(scene)) correction.y = bottom(scene) - bottom(entityBounds);
    } else {
        correction.y = (top(scene) + scene.height * 0.5f)
                     - (top(entityBounds) + entityBounds.height * 0.5f);
    }

    return Vec2{currentPosition.x + correction.x, currentPosition.y + correction.y};
}

} // namespace ArtCade::EditorNative
