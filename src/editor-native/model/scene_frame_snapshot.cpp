#include "editor-native/model/scene_frame_snapshot.h"

#include "editor-native/model/authored_transform.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tilemap_render_view.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

// One default grid cell (SceneGridDefaults::kCellSize): a sprite-less
// entity's placeholder box fills exactly one cell at the default grid.
constexpr float kDefaultSpriteExtent = 32.f;

const Vec3* fillFor(const ProjectDocument& document, const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    return it == types.end() ? nullptr : &it->second.sprite.fillColor;
}

SceneFrameTransform2D instanceVisual(const Transform& transform) {
    return projectTransform(transform, Vec2{kDefaultSpriteExtent, kDefaultSpriteExtent});
}

SceneFrameRect unrotatedRect(const SceneFrameTransform2D& xf) {
    return SceneFrameRect{
        xf.center.x - xf.size.x * 0.5f,
        xf.center.y - xf.size.y * 0.5f,
        xf.size.x,
        xf.size.y,
    };
}

WorldRect toWorldRect(const SceneFrameRect& rect) {
    return WorldRect{rect.x, rect.y, rect.width, rect.height};
}

WorldRect aabbWorldRect(const SceneFrameRect& dest, float rotationRadians) {
    const SceneFrameTransform2D xf{
        Vec2{dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f},
        Vec2{dest.width, dest.height},
        rotationRadians,
    };
    const TransformAabb aabb = aabbOfTransform(xf);
    return WorldRect{aabb.x, aabb.y, aabb.width, aabb.height};
}

SceneFrameTransform2D visualFromRect(const SceneFrameRect& dest, float rotationRadians) {
    return SceneFrameTransform2D{
        Vec2{dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f},
        Vec2{dest.width, dest.height},
        rotationRadians,
    };
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
        const SceneFrameTransform2D xf = instanceVisual(inst.transform);
        const SceneFrameRect bounds = unrotatedRect(xf);
        const Vec3* fill = fillFor(document, inst.objectTypeId);
        const bool selected = inst.id == selectedEntity;
        snapshot.entities.push_back(SceneFrameEntity{
            inst.id, inst.instanceName,
            fill ? *fill : Vec3{0.47f, 0.49f, 0.52f}, bounds, selected, xf.rotationRadians});
        const SpriteRenderView sprite = resolveSpriteRenderer(document, sceneId, inst.id);
        if (sprite.present && !sprite.assetId.empty()) {
            snapshot.sprites.push_back(SceneFrameSprite{
                inst.id, sprite.assetId, bounds,
                Vec2{bounds.width * 0.5f, bounds.height * 0.5f},
                sprite.visible, selected,
                SceneFrameRect{sprite.sourceRect.x, sprite.sourceRect.y,
                               sprite.sourceRect.w, sprite.sourceRect.h},
                sprite.hasSourceRect, xf.rotationRadians});
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

    for (const SceneInstanceDef* inst : document.instancesInRenderOrder(sceneId)) {
        if (!hiddenLayers.empty() && hiddenLayers.count(document.effectiveLayerId(sceneId, *inst))) {
            continue;
        }
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

    // Render order only (indices into scene.entities) - simulation
    // (advance/update/findEntity) always iterates scene.entities directly in
    // its own structural order, never this one.
    for (std::size_t index : scene.renderOrder) {
        const RuntimeEntity& entity = scene.entities[index];
        if (entity.destroyed || !entity.visible) continue;
        const SceneFrameTransform2D xf = instanceVisual(entity.transform);
        const SceneFrameRect bounds = unrotatedRect(xf);
        const bool hasSprite =
            entity.sprite.has_value() && !entity.sprite->assetId.empty();
        const bool paintedTilemap =
            entity.tilemap.has_value() && !entity.tilemap->cells.empty();
        // SceneView walks frame.entities in render order; tilemaps are drawn
        // per entity inside that loop. An unpainted tilemap-only entity stays
        // out of the snapshot entirely (no placeholder, no cells) - unlike
        // Edit, where the placeholder is a deliberate authoring affordance.
        const bool emptyTilemapOnly =
            entity.tilemap.has_value() && !paintedTilemap && !hasSprite;
        if (!emptyTilemapOnly) {
            snapshot.entities.push_back(SceneFrameEntity{
                entity.id,
                entity.name,
                entity.fillColor,
                bounds,
                false,
                xf.rotationRadians,
            });
        }

        if (entity.tilemap.has_value() && !entity.tilemap->cells.empty()) {
            // Destination is recomputed from the entity's *current* transform
            // every frame - never cached from Start Play - so the tilemap
            // keeps following its owning entity if a mover moves it.
            std::vector<SceneFrameTilemapCell> cells;
            cells.reserve(entity.tilemap->cells.size());
            for (const RuntimeTilemapCell& cell : entity.tilemap->cells) {
                cells.push_back(SceneFrameTilemapCell{
                    tilemapCellDestination(entity.transform.position, entity.tilemap->cellSize,
                                           cell.cellX, cell.cellY),
                    SceneFrameRect{cell.sourceRect.x, cell.sourceRect.y,
                                  cell.sourceRect.w, cell.sourceRect.h},
                });
            }
            snapshot.tilemaps.push_back(SceneFrameTilemap{
                entity.id, entity.tilemap->imageAssetId, std::move(cells), false});
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
                xf.rotationRadians,
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
    // frame.entities is the single authority for visual (and therefore pick)
    // order - already back-to-front by scene layer via
    // ProjectDocument::instancesInRenderOrder, the exact same sequence
    // SceneView::render() consumes forward. Walking it in reverse (front-to-
    // back) and returning the first hit mirrors that draw order exactly;
    // grouping by type instead (every tilemap, then every sprite) - the
    // previous implementation here - drops cross-type layer interleaving,
    // the same class of bug already fixed once for rendering itself. Each
    // entity's own sprite/tilemap (an instance can have both at once) is
    // checked before moving to the next entity, never across entities.
    for (auto it = frame.entities.rbegin(); it != frame.entities.rend(); ++it) {
        const EntityId id = it->entityId;
        bool hasVisual = false;
        for (const SceneFrameSprite& sprite : frame.sprites) {
            if (sprite.entityId != id) continue;
            hasVisual = true;
            if (sprite.visible
                && transformContainsPoint(
                    visualFromRect(sprite.destination, sprite.rotationRadians), worldPoint)) {
                return id;
            }
            break;   // one SpriteRenderer per entity
        }
        for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
            if (tilemap.entityId != id) continue;
            hasVisual = true;
            for (const SceneFrameTilemapCell& cell : tilemap.cells) {
                if (rectContains(cell.destination, worldPoint)) return id;
            }
            break;   // one Tilemap component per entity
        }
        if (!hasVisual
            && transformContainsPoint(
                visualFromRect(it->bounds, it->rotationRadians), worldPoint)) {
            return id;
        }
    }
    return INVALID_ENTITY;
}

std::optional<WorldRect> editorBoundsForEntity(const SceneFrameSnapshot& frame,
                                               EntityId entityId) {
    std::optional<WorldRect> bounds;
    for (const SceneFrameSprite& sprite : frame.sprites) {
        if (sprite.entityId != entityId || !sprite.visible || sprite.assetId.empty()) continue;
        const WorldRect rect = aabbWorldRect(sprite.destination, sprite.rotationRadians);
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
        const WorldRect rect = aabbWorldRect(entity.bounds, entity.rotationRadians);
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
