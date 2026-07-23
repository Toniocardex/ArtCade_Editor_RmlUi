#include "editor-native/model/scene_frame_snapshot.h"

#include "editor-native/model/authored_transform.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tilemap_render_view.h"

// RU-03: the runtime's own SceneFrameSnapshot/RenderableEntitySnapshot
// (distinct type from ArtCade::EditorNative::SceneFrameSnapshot above,
// disambiguated with the ArtCade:: prefix at each use below) and
// sprite_frame_has_pixels(), needed to read PlaySession::buildFrame().
#include "app/render/scene_frame_snapshot.h"
#include "app/render/sprite_frame_resolve.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
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
        // SceneInstanceDef::visible is the root authoring visibility. Unlike
        // SpriteRenderer.visible it gates every visual owned by the instance:
        // placeholder, sprite, tilemap, collider overlay, bounds and picking.
        if (!inst->visible) continue;
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

// RU-03: sprite/transform/visibility come from GameplaySession renderables.
// Entity-owned tilemaps are editor Play-only (ADR-0001): compiled into
// PlaySession at materialize and projected here each frame from the entity's
// *current* transform when present in renderables, else the authored origin.
SceneFrameSnapshot collectSceneFrameSnapshot(const PlaySession& session) {
    SceneFrameSnapshot snapshot;
    const PlaySceneInfo& scene = session.scene();
    snapshot.sceneId = scene.sourceSceneId;
    snapshot.hasScene = true;
    snapshot.sceneName = scene.name;
    snapshot.worldSize = scene.worldSize;
    snapshot.backgroundColor = scene.backgroundColor;

    const std::vector<ArtCade::RenderableEntitySnapshot> renderables = session.renderables();
    std::unordered_map<EntityId, const ArtCade::RenderableEntitySnapshot*> renderableById;
    renderableById.reserve(renderables.size());
    for (const ArtCade::RenderableEntitySnapshot& entity : renderables) {
        renderableById.emplace(entity.id, &entity);
    }

    std::unordered_map<EntityId, const PlayTilemap*> tilemapById;
    tilemapById.reserve(session.tilemaps().size());
    for (const PlayTilemap& tilemap : session.tilemaps()) {
        tilemapById.emplace(tilemap.entityId, &tilemap);
    }

    const auto emitSprite = [&](const ArtCade::RenderableEntitySnapshot& entity,
                                const SceneFrameRect& bounds, float rotationRadians) {
        const AssetId resolvedAssetId = entity.spriteFrame.assetId.empty()
            ? entity.sprite.spriteAssetId : entity.spriteFrame.assetId;
        if (resolvedAssetId.empty()) return;
        const bool hasSource = AppRender::sprite_frame_has_pixels(entity.spriteFrame.frame);
        snapshot.sprites.push_back(SceneFrameSprite{
            entity.id,
            resolvedAssetId,
            bounds,
            Vec2{bounds.width * 0.5f, bounds.height * 0.5f},
            /*visible=*/true,
            false,
            hasSource
                ? SceneFrameRect{static_cast<float>(entity.spriteFrame.frame.x),
                                 static_cast<float>(entity.spriteFrame.frame.y),
                                 static_cast<float>(entity.spriteFrame.frame.w),
                                 static_cast<float>(entity.spriteFrame.frame.h)}
                : SceneFrameRect{},
            hasSource,
            rotationRadians,
            entity.sprite.flipX,
            entity.sprite.flipY,
        });
    };

    const auto emitTilemap = [&](const PlayTilemap& tilemap, Vec2 origin) {
        if (tilemap.cells.empty()) return;
        std::vector<SceneFrameTilemapCell> cells;
        cells.reserve(tilemap.cells.size());
        for (const PlayTilemapCell& cell : tilemap.cells) {
            cells.push_back(SceneFrameTilemapCell{
                tilemapCellDestination(origin, tilemap.cellSize, cell.cellX, cell.cellY),
                SceneFrameRect{cell.sourceX, cell.sourceY, cell.sourceW, cell.sourceH},
            });
        }
        snapshot.tilemaps.push_back(SceneFrameTilemap{
            tilemap.entityId, tilemap.imageAssetId, std::move(cells), false});
    };

    // Walk the materialize-time layer order so tilemaps interleave with sprites.
    std::unordered_set<EntityId> visited;
    for (const EntityId id : session.renderEntityOrder()) {
        visited.insert(id);
        const auto rendIt = renderableById.find(id);
        const ArtCade::RenderableEntitySnapshot* rend =
            rendIt != renderableById.end() ? rendIt->second : nullptr;
        const auto tmIt = tilemapById.find(id);
        const PlayTilemap* tilemap = tmIt != tilemapById.end() ? tmIt->second : nullptr;
        const bool entityVisible = rend ? rend->visibleInGame
                                        : (tilemap && tilemap->visible);
        const bool showTilemap = entityVisible && tilemap && !tilemap->cells.empty();
        const bool showRenderable = rend && rend->visibleInGame;
        if (!showRenderable && !showTilemap) continue;

        if (showRenderable) {
            const SceneFrameTransform2D xf = instanceVisual(rend->transform);
            const SceneFrameRect bounds = unrotatedRect(xf);
            snapshot.entities.push_back(SceneFrameEntity{
                rend->id, std::string{}, rend->sprite.fillColor, bounds, false,
                xf.rotationRadians});
            emitSprite(*rend, bounds, xf.rotationRadians);
            if (showTilemap) emitTilemap(*tilemap, rend->transform.position);
        } else {
            // Tilemap-only (no sprite renderable): draw cells, never the Edit
            // placeholder box.
            Transform authored;
            authored.position = tilemap->authoredOrigin;
            const SceneFrameTransform2D xf = instanceVisual(authored);
            snapshot.entities.push_back(SceneFrameEntity{
                id, std::string{}, Vec3{}, unrotatedRect(xf), false, xf.rotationRadians});
            emitTilemap(*tilemap, tilemap->authoredOrigin);
        }
    }

    // Any renderable not in the captured order (defensive) still draws.
    for (const ArtCade::RenderableEntitySnapshot& entity : renderables) {
        if (!entity.visibleInGame || visited.count(entity.id) != 0) continue;
        const SceneFrameTransform2D xf = instanceVisual(entity.transform);
        const SceneFrameRect bounds = unrotatedRect(xf);
        snapshot.entities.push_back(SceneFrameEntity{
            entity.id, std::string{}, entity.sprite.fillColor, bounds, false,
            xf.rotationRadians});
        emitSprite(entity, bounds, xf.rotationRadians);
        if (const auto tmIt = tilemapById.find(entity.id); tmIt != tilemapById.end()) {
            emitTilemap(*tmIt->second, entity.transform.position);
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
    // back) and returning the first hit mirrors that draw order exactly.
    for (auto it = frame.entities.rbegin(); it != frame.entities.rend(); ++it) {
        const EntityId id = it->entityId;
        for (const SceneFrameSprite& sprite : frame.sprites) {
            if (sprite.entityId != id) continue;
            if (sprite.visible
                && transformContainsPoint(
                    visualFromRect(sprite.destination, sprite.rotationRadians), worldPoint)) {
                return id;
            }
            break;   // one SpriteRenderer per entity
        }
        bool hasPopulatedTilemap = false;
        for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
            if (tilemap.entityId != id) continue;
            hasPopulatedTilemap = !tilemap.cells.empty();
            for (const SceneFrameTilemapCell& cell : tilemap.cells) {
                if (rectContains(cell.destination, worldPoint)) return id;
            }
            break;   // one Tilemap component per entity
        }
        // A populated Tilemap's hit area is exactly its painted cells, just
        // checked above - a click inside the entity's placeholder box but
        // outside every cell (e.g. a gap in a sparse tilemap, or anywhere
        // once cells extend past the placeholder) must miss rather than fall
        // back to the placeholder, or the placeholder would stay a second,
        // disconnected hit target over content that already owns the area.
        if (hasPopulatedTilemap) continue;
        // Placeholder body + a short band above for the on-screen name chip.
        // Always available as fallback so invisible sprites / empty tilemaps
        // (still drawn as placeholders) remain pickable.
        SceneFrameRect hitBounds = it->bounds;
        constexpr float kLabelBandWu = 12.f;
        hitBounds.y -= kLabelBandWu;
        hitBounds.height += kLabelBandWu;
        if (transformContainsPoint(
                visualFromRect(hitBounds, it->rotationRadians), worldPoint)) {
            return id;
        }
    }
    return INVALID_ENTITY;
}

void applyDragPreviewOffset(SceneFrameSnapshot& snapshot, EntityId entity, Vec2 delta) {
    for (SceneFrameEntity& e : snapshot.entities) {
        if (e.entityId == entity) { e.bounds.x += delta.x; e.bounds.y += delta.y; }
    }
    for (SceneFrameSprite& s : snapshot.sprites) {
        if (s.entityId == entity) { s.destination.x += delta.x; s.destination.y += delta.y; }
    }
    for (SceneFrameCollider& col : snapshot.colliders) {
        if (col.entityId == entity) { col.worldBounds.x += delta.x; col.worldBounds.y += delta.y; }
    }
    for (SceneFrameTilemap& tm : snapshot.tilemaps) {
        if (tm.entityId != entity) continue;
        for (SceneFrameTilemapCell& cell : tm.cells) {
            cell.destination.x += delta.x;
            cell.destination.y += delta.y;
        }
    }
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
