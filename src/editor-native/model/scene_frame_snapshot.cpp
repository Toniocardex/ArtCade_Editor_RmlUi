#include "editor-native/model/scene_frame_snapshot.h"

#include "editor-native/model/authored_transform.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/presentation_variable_refs.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/text_layout_math.h"
#include "editor-native/model/tilemap_render_view.h"
#include "core/text-component-format.h"

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
                                             const std::unordered_set<std::string>& hiddenLayers,
                                             const SceneTransformPreview* preview) {
    SceneFrameSnapshot snapshot;
    snapshot.sceneId = sceneId;

    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) return snapshot;

    snapshot.hasScene = true;
    snapshot.sceneName = scene->name;
    snapshot.worldSize = scene->worldSize;
    snapshot.backgroundColor = scene->backgroundColor;

    std::unordered_set<EntityId> emitted;   // for filtering the collider overlay
    const auto emit = [&](const SceneInstanceDef& inst) {
        const Transform& effectiveTransform =
            (preview && preview->entityId == inst.id) ? preview->transform : inst.transform;
        const SceneFrameTransform2D xf = instanceVisual(effectiveTransform);
        const SceneFrameRect bounds = unrotatedRect(xf);
        const Vec3* fill = fillFor(document, inst.objectTypeId);
        const bool selected = inst.id == selectedEntity;
        // SceneInstanceDef::visible ("Entity Visible" in the Inspector) is the
        // root authoring visibility. Edit mode still draws every visual owned
        // by the instance when it's off - placeholder, sprite, tilemap, text,
        // gauge - just dimmed (visibleInGame=false), the same inEditMode-dims/
        // actual-gameplay-hides split scene_entities_pass.cpp already uses for
        // Play. A hard omission here would make the entity unselectable while
        // editing, which is what the Layer Manager's hidden-layer toggle
        // (a separate, editor-only declutter concept) deliberately does
        // instead - see the loop below.
        SceneFrameEntity entityEntry{
            inst.id, document.instanceDisplayName(sceneId, inst.id),
            fill ? *fill : Vec3{0.47f, 0.49f, 0.52f}, bounds, selected, xf.rotationRadians};
        entityEntry.visibleInGame = inst.visible;
        snapshot.entities.push_back(entityEntry);
        const SpriteRenderView sprite = resolveSpriteRenderer(document, sceneId, inst.id);
        if (sprite.present && !sprite.assetId.empty()) {
            SceneFrameSprite spriteEntry{
                inst.id, sprite.assetId, bounds,
                Vec2{bounds.width * 0.5f, bounds.height * 0.5f},
                sprite.visible, selected,
                SceneFrameRect{sprite.sourceRect.x, sprite.sourceRect.y,
                               sprite.sourceRect.w, sprite.sourceRect.h},
                sprite.hasSourceRect, xf.rotationRadians};
            spriteEntry.visibleInGame = inst.visible;
            snapshot.sprites.push_back(spriteEntry);
        }
        if (inst.tilemap.has_value()) {
            // A dangling tilesetAssetId shouldn't survive validation, but if one
            // reaches here anyway the tilemap simply isn't emitted, rather than
            // emitted with a blank image id the renderer would need to guard.
            if (const TilesetAsset* tileset =
                    document.findTilesetAsset(inst.tilemap->tilesetAssetId)) {
                SceneFrameTilemap tilemapEntry{
                    inst.id, tileset->imageAssetId,
                    tilemapRenderCells(*inst.tilemap, *tileset, effectiveTransform.position),
                    selected};
                tilemapEntry.visibleInGame = inst.visible;
                snapshot.tilemaps.push_back(std::move(tilemapEntry));
            }
        }
        const EntityDef* type = document.findObjectType(inst.objectTypeId);
        if (type && type->text
            && (!type->text->text.empty() || !type->text->bindKey.empty())) {
            const std::optional<GameVariableValue> bound =
                resolveEffectiveBoundInitialValue(
                    document.data(), inst, *type, type->text->bindKey, type->text->bindScope);
            SceneFrameText entry;
            entry.entityId = inst.id;
            entry.displayText = resolveTextDisplay(*type->text, bound);
            entry.anchorPosition = Vec2{
                effectiveTransform.position.x + type->text->offsetX,
                effectiveTransform.position.y + type->text->offsetY,
            };
            entry.align = type->text->align;
            entry.size = type->text->size;
            entry.color = type->text->color;
            entry.screenSpace = type->text->screenSpace;
            entry.layerOpacity = 1.f;
            entry.fontPath = type->text->fontPath;
            entry.visibleInGame = inst.visible;
            snapshot.texts.push_back(std::move(entry));
        }
        if (type && type->gauge && type->gauge->width > 0.f && type->gauge->height > 0.f) {
            float value = type->gauge->maxValue;
            if (const std::optional<GameVariableValue> bound =
                    resolveEffectiveBoundInitialValue(document.data(), inst, *type,
                                                     type->gauge->bindKey,
                                                     type->gauge->bindScope)) {
                if (const auto* number = std::get_if<double>(&*bound)) {
                    value = static_cast<float>(*number);
                }
            }
            float ratio = type->gauge->maxValue > 0.f ? value / type->gauge->maxValue : 0.f;
            ratio = std::clamp(ratio, 0.f, 1.f);
            SceneFrameGauge gaugeEntry{
                inst.id,
                Vec2{effectiveTransform.position.x + type->gauge->offsetX,
                     effectiveTransform.position.y + type->gauge->offsetY},
                type->gauge->width,
                type->gauge->height,
                type->gauge->fillColor,
                type->gauge->bgColor,
                ratio,
                type->gauge->direction,
                type->gauge->screenSpace,
            };
            gaugeEntry.visibleInGame = inst.visible;
            snapshot.gauges.push_back(gaugeEntry);
        }
        emitted.insert(inst.id);
    };

    for (const SceneInstanceDef* inst : document.instancesInRenderOrder(sceneId)) {
        // Layer-hidden (Layer Manager eye icon) is the one case that stays a
        // hard cut: a pure editor-canvas declutter toggle, unrelated to
        // gameplay visibility - see the comment in emit() above for why
        // SceneInstanceDef::visible itself no longer cuts here.
        if (!hiddenLayers.empty() && hiddenLayers.count(document.effectiveLayerId(sceneId, *inst))) {
            continue;
        }
        emit(*inst);
    }

    // Collider overlays follow the same set as their entities (still an
    // editing aid for an "Entity Visible"-off instance, same as its
    // placeholder/sprite staying selectable rather than disappearing).
    snapshot.colliders = collectBoxColliderBounds(document, sceneId, selectedEntity, preview);
    snapshot.colliders.erase(
        std::remove_if(snapshot.colliders.begin(), snapshot.colliders.end(),
                       [&](const SceneFrameCollider& c) { return emitted.count(c.entityId) == 0; }),
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
        const bool hasSprite = rend
            && (!rend->sprite.spriteAssetId.empty() || !rend->spriteFrame.assetId.empty());
        const bool hasOtherVisual = rend && (rend->text.has_value() || rend->gauge.has_value());
        // Play never renders the editor's generic placeholder. A tilemap owner
        // with no painted cells and no independent visual is therefore absent.
        const bool emptyTilemapOnly = tilemap && tilemap->cells.empty()
            && !hasSprite && !hasOtherVisual;
        const bool showRenderable = rend && rend->visibleInGame && !emptyTilemapOnly;
        if (!showRenderable && !showTilemap) continue;

        if (showRenderable) {
            const SceneFrameTransform2D xf = instanceVisual(rend->transform);
            const SceneFrameRect bounds = unrotatedRect(xf);
            snapshot.entities.push_back(SceneFrameEntity{
                rend->id, std::string{}, rend->sprite.fillColor, bounds, false,
                xf.rotationRadians});
            emitSprite(*rend, bounds, xf.rotationRadians);
            if (showTilemap) emitTilemap(*tilemap, rend->transform.position);
            if (rend->text) {
                SceneFrameText entry;
                entry.entityId = rend->id;
                entry.displayText = rend->text->text; // already resolved
                entry.anchorPosition = Vec2{
                    rend->transform.position.x + rend->text->offsetX,
                    rend->transform.position.y + rend->text->offsetY,
                };
                entry.align = rend->text->align;
                entry.size = rend->text->size;
                entry.color = rend->text->color;
                entry.screenSpace = rend->text->screenSpace;
                entry.layerOpacity = 1.f;
                entry.fontPath = rend->text->fontPath;
                snapshot.texts.push_back(std::move(entry));
            }
            if (rend->gauge) {
                snapshot.gauges.push_back(SceneFrameGauge{
                    rend->id,
                    Vec2{rend->transform.position.x + rend->gauge->offsetX,
                         rend->transform.position.y + rend->gauge->offsetY},
                    rend->gauge->width,
                    rend->gauge->height,
                    rend->gauge->fillColor,
                    rend->gauge->bgColor,
                    rend->gaugeRatio,
                    rend->gauge->direction,
                    rend->gauge->screenSpace,
                });
            }
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
    return pickEntityAt(frame, ScenePickPoint{worldPoint, worldPoint});
}

EntityId pickEntityAt(const SceneFrameSnapshot& frame, ScenePickPoint point) {
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
                    visualFromRect(sprite.destination, sprite.rotationRadians), point.world)) {
                return id;
            }
            break;   // one SpriteRenderer per entity
        }
        bool hasPopulatedTilemap = false;
        for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
            if (tilemap.entityId != id) continue;
            hasPopulatedTilemap = !tilemap.cells.empty();
            for (const SceneFrameTilemapCell& cell : tilemap.cells) {
                if (rectContains(cell.destination, point.world)) return id;
            }
            break;   // one Tilemap component per entity
        }
        bool hasWorldText = false;
        for (const SceneFrameText& text : frame.texts) {
            if (text.entityId != id) continue;
            const TextVisualLayout layout = estimateSceneFrameTextLayout(text);
            if (text.screenSpace) {
                if (rectContains(layout.bounds, point.viewport)) return id;
            } else {
                hasWorldText = true;
                if (rectContains(layout.bounds, point.world)) return id;
            }
        }
        for (const SceneFrameGauge& gauge : frame.gauges) {
            if (gauge.entityId != id) continue;
            const SceneFrameRect bounds{
                gauge.anchorPosition.x, gauge.anchorPosition.y, gauge.width, gauge.height};
            if (gauge.screenSpace) {
                if (rectContains(bounds, point.viewport)) return id;
            } else {
                hasWorldText = true;
                if (rectContains(bounds, point.world)) return id;
            }
        }
        // A populated Tilemap's hit area is exactly its painted cells, just
        // checked above - a click inside the entity's placeholder box but
        // outside every cell (e.g. a gap in a sparse tilemap, or anywhere
        // once cells extend past the placeholder) must miss rather than fall
        // back to the placeholder, or the placeholder would stay a second,
        // disconnected hit target over content that already owns the area.
        if (hasPopulatedTilemap) continue;
        if (hasWorldText) {
            // Text/gauge-only (or with invisible sprite): do not fall back to
            // the 32×32 placeholder when presentation visuals exist.
            bool hasVisibleSprite = false;
            for (const SceneFrameSprite& sprite : frame.sprites) {
                if (sprite.entityId == id && sprite.visible && !sprite.assetId.empty()) {
                    hasVisibleSprite = true;
                    break;
                }
            }
            if (!hasVisibleSprite) continue;
        }
        // Placeholder body + a short band above for the on-screen name chip.
        // Always available as fallback so invisible sprites / empty tilemaps
        // (still drawn as placeholders) remain pickable.
        SceneFrameRect hitBounds = it->bounds;
        constexpr float kLabelBandWu = 12.f;
        hitBounds.y -= kLabelBandWu;
        hitBounds.height += kLabelBandWu;
        if (transformContainsPoint(
                visualFromRect(hitBounds, it->rotationRadians), point.world)) {
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
    for (SceneFrameText& text : snapshot.texts) {
        if (text.entityId == entity && !text.screenSpace) {
            text.anchorPosition.x += delta.x;
            text.anchorPosition.y += delta.y;
        }
    }
    for (SceneFrameGauge& gauge : snapshot.gauges) {
        if (gauge.entityId == entity && !gauge.screenSpace) {
            gauge.anchorPosition.x += delta.x;
            gauge.anchorPosition.y += delta.y;
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
    for (const SceneFrameText& text : frame.texts) {
        if (text.entityId != entityId || text.screenSpace) continue;
        const TextVisualLayout layout = estimateSceneFrameTextLayout(text);
        const WorldRect rect = toWorldRect(layout.bounds);
        if (!finiteRect(rect)) continue;
        bounds = bounds ? unite(*bounds, rect) : rect;
    }
    for (const SceneFrameGauge& gauge : frame.gauges) {
        if (gauge.entityId != entityId || gauge.screenSpace) continue;
        const WorldRect rect{
            gauge.anchorPosition.x, gauge.anchorPosition.y, gauge.width, gauge.height};
        if (!finiteRect(rect)) continue;
        bounds = bounds ? unite(*bounds, rect) : rect;
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
