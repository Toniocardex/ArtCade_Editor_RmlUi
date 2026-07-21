#include "editor-native/view/scene_view.h"

#include "editor-native/model/authored_transform.h"
#include "editor-native/model/tilemap_render_view.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

namespace {

// Pixel rectangle for a scissor region.
struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool valid() const { return width > 0 && height > 0; }
};

// Intersection of the viewport with the scene's on-screen rectangle, so world
// drawing is clipped to the scene surface rather than the whole panel.
PixelRect sceneScissor(const ViewportRect& rect, Vector2 topLeft, Vector2 bottomRight) {
    const int x0 = std::max(rect.x, static_cast<int>(std::floor(topLeft.x)));
    const int y0 = std::max(rect.y, static_cast<int>(std::floor(topLeft.y)));
    const int x1 = std::min(rect.x + rect.width,  static_cast<int>(std::ceil(bottomRight.x)));
    const int y1 = std::min(rect.y + rect.height, static_cast<int>(std::ceil(bottomRight.y)));
    return PixelRect{x0, y0, x1 - x0, y1 - y0};
}

Color toColor(const Vec4& c) {
    return Color{
        static_cast<unsigned char>(c.r * 255.f),
        static_cast<unsigned char>(c.g * 255.f),
        static_cast<unsigned char>(c.b * 255.f),
        static_cast<unsigned char>(c.a * 255.f),
    };
}

Color toColor(const Vec3& c, float alpha = 1.f) {
    return toColor(Vec4{c.x, c.y, c.z, alpha});
}

Rectangle toRectangle(const SceneFrameRect& rect) {
    return Rectangle{rect.x, rect.y, rect.width, rect.height};
}

// Snapshot stores unrotated top-left rects. Raylib Draw*Pro treats dest.x/y as
// the world position of the origin pivot (and subtracts origin even at 0°), so
// convert top-left → pivot before drawing.
Rectangle pivotDestination(const SceneFrameRect& topLeft, Vector2 origin) {
    return Rectangle{
        topLeft.x + origin.x,
        topLeft.y + origin.y,
        topLeft.width,
        topLeft.height,
    };
}

SceneFrameTransform2D visualOf(const SceneFrameRect& dest, float rotationRadians) {
    return SceneFrameTransform2D{
        Vec2{dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f},
        Vec2{dest.width, dest.height},
        rotationRadians,
    };
}

void cornersOf(const SceneFrameTransform2D& xf, Vector2 out[4]) {
    const float hx = xf.size.x * 0.5f;
    const float hy = xf.size.y * 0.5f;
    const float c = std::cos(xf.rotationRadians);
    const float s = std::sin(xf.rotationRadians);
    const Vec2 local[4] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
    for (int i = 0; i < 4; ++i) {
        out[i] = {
            xf.center.x + local[i].x * c - local[i].y * s,
            xf.center.y + local[i].x * s + local[i].y * c,
        };
    }
}

void drawOrientedOutline(const SceneFrameTransform2D& xf, float thickness, Color color) {
    Vector2 corners[4];
    cornersOf(xf, corners);
    for (int i = 0; i < 4; ++i) {
        DrawLineEx(corners[i], corners[(i + 1) % 4], thickness, color);
    }
}

bool hasVisibleSprite(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameSprite& sprite : frame.sprites) {
        if (sprite.entityId == entityId && sprite.visible && !sprite.assetId.empty()) return true;
    }
    return false;
}

// An entity with a Tilemap component but nothing painted yet still falls
// through to the generic placeholder box below - the same "content-less
// entity" marker every other component-less entity already gets, rather
// than a bespoke "empty tilemap" graphic.
bool hasVisibleTilemapCells(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
        if (tilemap.entityId == entityId && !tilemap.cells.empty()) return true;
    }
    return false;
}

// Unrotated AABB spanning every painted cell of an entity's Tilemap - built
// from the same destination rects the render pass above already draws, so
// the selection outline matches the painted area instead of staying pinned
// to the entity's small placeholder box (which can sit far from the tiles
// once the tilemap has been painted with an offset).
std::optional<SceneFrameRect> tilemapCellBounds(const SceneFrameSnapshot& frame,
                                                 EntityId entityId) {
    std::optional<SceneFrameRect> bounds;
    for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
        if (tilemap.entityId != entityId) continue;
        for (const SceneFrameTilemapCell& cell : tilemap.cells) {
            const SceneFrameRect& d = cell.destination;
            if (!bounds) {
                bounds = d;
                continue;
            }
            const float minX = std::min(bounds->x, d.x);
            const float minY = std::min(bounds->y, d.y);
            const float maxX = std::max(bounds->x + bounds->width, d.x + d.width);
            const float maxY = std::max(bounds->y + bounds->height, d.y + d.height);
            bounds = SceneFrameRect{minX, minY, maxX - minX, maxY - minY};
        }
        break;   // one Tilemap component per entity
    }
    return bounds;
}

void drawMissingSprite(const SceneFrameSprite& sprite, float zoom) {
    const SceneFrameTransform2D xf = visualOf(sprite.destination, sprite.rotationRadians);
    const float degrees = sprite.rotationRadians * kRadToDeg;
    const Vector2 origin{sprite.origin.x, sprite.origin.y};
    DrawRectanglePro(pivotDestination(sprite.destination, origin), origin, degrees,
                     Color{70, 44, 58, 200});
    drawOrientedOutline(xf, 1.5f / zoom, Color{230, 90, 120, 230});
    Vector2 corners[4];
    cornersOf(xf, corners);
    DrawLineEx(corners[0], corners[2], 1.2f / zoom, Color{230, 90, 120, 230});
    DrawLineEx(corners[1], corners[3], 1.2f / zoom, Color{230, 90, 120, 230});
}

void drawDashedLine(Vector2 a, Vector2 b, float thickness, Color color) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.f) return;
    constexpr float dash = 6.f;
    constexpr float gap = 4.f;
    const Vector2 dir{dx / length, dy / length};
    for (float t = 0.f; t < length; t += dash + gap) {
        const float end = std::min(t + dash, length);
        DrawLineEx({a.x + dir.x * t, a.y + dir.y * t},
                   {a.x + dir.x * end, a.y + dir.y * end},
                   thickness, color);
    }
}

void drawDashedRectangle(Rectangle r, float thickness, Color color) {
    drawDashedLine({r.x, r.y}, {r.x + r.width, r.y}, thickness, color);
    drawDashedLine({r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, thickness, color);
    drawDashedLine({r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}, thickness, color);
    drawDashedLine({r.x, r.y + r.height}, {r.x, r.y}, thickness, color);
}

} // namespace

void SceneView::render(const SceneFrameSnapshot& frame,
                       const EditorSceneViewState& view,
                       const SceneGridDefinition& displayGrid,
                       const SceneViewportProjection& projection,
                       const TextureCache& textures,
                       const CanvasFont& canvasFont) const {
    const ViewportRect& rect = projection.visibleRect;
    if (!rect.valid()) return;

    BeginScissorMode(rect.x, rect.y, rect.width, rect.height);

    DrawRectangle(rect.x, rect.y, rect.width, rect.height, Color{14, 14, 16, 255});

    if (!frame.hasScene) {
        // The RmlUi #viewport-empty overlay carries the guidance; the backdrop
        // fill above is all raylib draws here.
        EndScissorMode();
        return;
    }

    const Vector2 world{frame.worldSize.x, frame.worldSize.y};
    const SceneViewCamera& vc = projection.camera;
    Camera2D cam{};
    cam.offset = Vector2{vc.offset.x, vc.offset.y};
    cam.target = Vector2{vc.target.x, vc.target.y};
    cam.zoom = vc.zoom;
    cam.rotation = 0.f;

    // Clip world-space drawing to the scene surface (intersected with the
    // viewport): entities that drift outside the scene must not paint over the
    // panel backdrop. The backdrop fill above stays at viewport scope.
    const PixelRect sceneClip = sceneScissor(
        rect, GetWorldToScreen2D(Vector2{0.f, 0.f}, cam),
        GetWorldToScreen2D(Vector2{world.x, world.y}, cam));
    if (sceneClip.valid()) {
    BeginScissorMode(sceneClip.x, sceneClip.y, sceneClip.width, sceneClip.height);
    BeginMode2D(cam);

    DrawRectangle(0, 0, static_cast<int>(world.x), static_cast<int>(world.y),
                  toColor(frame.backgroundColor));

    if (view.gridVisible) {
        // Zinc grid: keep snap on the logical cell while thinning visual lines
        // at low zoom or very small cells. displayGrid is resolved by the caller
        // from the active tool (world authoring vs tilemap cell grid).
        const SceneGridDefinition& grid = displayGrid;
        const int visualStrideX = visualGridStrideForZoom(grid.cellSize.x, cam.zoom);
        const int visualStrideY = visualGridStrideForZoom(grid.cellSize.y, cam.zoom);
        const float visualStepX = grid.cellSize.x * static_cast<float>(visualStrideX);
        const float visualStepY = grid.cellSize.y * static_cast<float>(visualStrideY);
        const Color gridMinor{120, 120, 130, 36};
        const Color gridMajor{120, 120, 130, 68};
        const auto firstLine = [](float origin, float step) {
            return origin + std::ceil((0.0f - origin) / step) * step;
        };
        if (visualStepX > 0.0f && std::isfinite(visualStepX)) {
            int ix = static_cast<int>(
                std::round((firstLine(grid.origin.x, visualStepX) - grid.origin.x)
                           / grid.cellSize.x));
            for (float gx = firstLine(grid.origin.x, visualStepX); gx <= world.x;
                 gx += visualStepX, ix += visualStrideX) {
                DrawLineV({gx, 0.f}, {gx, world.y}, (ix % 4 == 0) ? gridMajor : gridMinor);
            }
        }
        if (visualStepY > 0.0f && std::isfinite(visualStepY)) {
            int iy = static_cast<int>(
                std::round((firstLine(grid.origin.y, visualStepY) - grid.origin.y)
                           / grid.cellSize.y));
            for (float gy = firstLine(grid.origin.y, visualStepY); gy <= world.y;
                 gy += visualStepY, iy += visualStrideY) {
                DrawLineV({0.f, gy}, {world.x, gy}, (iy % 4 == 0) ? gridMajor : gridMinor);
            }
        }
    }

    // Neutral world frame — clearly marks where the world ends, while the accent
    // selection still stands out against it.
    const float linePx = 2.f / cam.zoom;
    DrawRectangleLinesEx(Rectangle{0, 0, world.x, world.y}, linePx, Color{92, 92, 102, 255});

    // One pass, in frame.entities' own order (already back-to-front by scene
    // layer, via ProjectDocument::instancesInRenderOrder) - each entity draws
    // whatever visual it has before moving to the next. Splitting this into
    // separate "draw every tilemap" / "draw every sprite" passes (as this used
    // to do) silently drops cross-type layer interleaving: a tilemap on a layer
    // above a sprite would always land underneath it, since every tilemap was
    // drawn before every sprite regardless of layer order. World-space
    // coordinates throughout: raylib's Camera2D (already active via
    // BeginMode2D) handles the screen mapping.
    for (const SceneFrameEntity& entity : frame.entities) {
        const bool hasSprite = hasVisibleSprite(frame, entity.entityId);
        const bool hasTilemap = hasVisibleTilemapCells(frame, entity.entityId);
        if (!hasSprite && !hasTilemap) {
            const SceneFrameTransform2D xf = visualOf(entity.bounds, entity.rotationRadians);
            const float degrees = entity.rotationRadians * kRadToDeg;
            const Vector2 origin{entity.bounds.width * 0.5f, entity.bounds.height * 0.5f};
            DrawRectanglePro(pivotDestination(entity.bounds, origin), origin, degrees,
                             toColor(entity.fillColor, 0.92f));
            drawOrientedOutline(xf, 1.f / cam.zoom, Color{12, 14, 18, 200});
            continue;
        }
        if (hasTilemap) {
            for (const SceneFrameTilemap& tilemap : frame.tilemaps) {
                if (tilemap.entityId != entity.entityId) continue;
                const TextureResource* resource = textures.find(tilemap.imageAssetId);
                if (resource && resource->loaded) {
                    for (const SceneFrameTilemapCell& cell : tilemap.cells) {
                        DrawTexturePro(resource->texture,
                                      toRectangle(tilemapAtlasSourceRect(cell.source)),
                                      toRectangle(cell.destination), Vector2{0.f, 0.f}, 0.f, WHITE);
                    }
                }
                break;   // one Tilemap component per entity
            }
        }
        if (hasSprite) {
            for (const SceneFrameSprite& sprite : frame.sprites) {
                if (sprite.entityId != entity.entityId) continue;
                const TextureResource* resource = textures.find(sprite.assetId);
                if (!resource || !resource->loaded) {
                    drawMissingSprite(sprite, cam.zoom);
                } else {
                    const Rectangle source = sprite.hasSource
                        ? toRectangle(sprite.source)
                        : Rectangle{0.f, 0.f,
                                    static_cast<float>(resource->texture.width),
                                    static_cast<float>(resource->texture.height)};
                    const Vector2 origin{sprite.origin.x, sprite.origin.y};
                    DrawTexturePro(resource->texture, source,
                                  pivotDestination(sprite.destination, origin), origin,
                                  sprite.rotationRadians * kRadToDeg, WHITE);
                }
                break;   // one SpriteRenderer per entity
            }
        }
    }

    for (const SceneFrameEntity& entity : frame.entities) {
        for (const SceneFrameCollider& collider : frame.colliders) {
            if (collider.entityId != entity.entityId) continue;
            const Rectangle bounds{
                collider.worldBounds.x,
                collider.worldBounds.y,
                collider.worldBounds.width,
                collider.worldBounds.height,
            };
            Color color = Color{88, 220, 140, 210};
            if (collider.mode == BoxColliderMode::Trigger) {
                color = Color{86, 180, 235, 210};
            } else if (collider.mode == BoxColliderMode::OneWayPlatform) {
                color = Color{216, 180, 74, 220};
            }
            const float colliderLine = (collider.selected ? 2.2f : 1.5f) / cam.zoom;
            if (collider.mode == BoxColliderMode::Trigger) {
                drawDashedRectangle(bounds, colliderLine, color);
            } else {
                DrawRectangleLinesEx(bounds, colliderLine, color);
                if (collider.mode == BoxColliderMode::OneWayPlatform) {
                    DrawLineEx({bounds.x, bounds.y},
                               {bounds.x + bounds.width, bounds.y},
                               (collider.selected ? 3.0f : 2.2f) / cam.zoom,
                               Color{250, 204, 80, 245});
                }
            }
            break;
        }

        if (entity.selected) {
            // Prefer the entity/sprite oriented visual for the selection outline
            // so a rotated instance does not get an AABB box disconnected from
            // what is drawn. Collider still only contributes to containment via
            // editorBoundsForEntity, but a populated Tilemap has no single
            // "visual" like a sprite does, so its outline instead spans every
            // painted cell - otherwise the outline would stay pinned to the
            // small placeholder box while the actual tiles render elsewhere.
            SceneFrameTransform2D outline = visualOf(entity.bounds, entity.rotationRadians);
            bool matchedContent = false;
            for (const SceneFrameSprite& sprite : frame.sprites) {
                if (sprite.entityId != entity.entityId || !sprite.visible
                    || sprite.assetId.empty()) {
                    continue;
                }
                outline = visualOf(sprite.destination, sprite.rotationRadians);
                matchedContent = true;
                break;
            }
            if (!matchedContent) {
                // Tiles render unrotated (see the DrawTexturePro call above),
                // so the outline must be too - rotating it would disagree with
                // what is actually on screen.
                if (const std::optional<SceneFrameRect> tilemapBounds =
                        tilemapCellBounds(frame, entity.entityId)) {
                    outline = visualOf(*tilemapBounds, 0.f);
                }
            }
            // Inflate slightly in local space for a readable pad around the visual.
            outline.size.x += 6.f;
            outline.size.y += 6.f;
            drawOrientedOutline(outline, 2.f / cam.zoom, Color{59, 130, 246, 255});
        }
    }

    EndMode2D();
    }

    // The scene-name chip is a viewport-space overlay, not clipped to the scene.
    BeginScissorMode(rect.x, rect.y, rect.width, rect.height);
    // Scene name — subtle rounded chip in the top-left corner of the viewport.
    for (const SceneFrameEntity& entity : frame.entities) {
        if (!entity.selected) continue;
        const std::optional<WorldRect> bounds = editorBoundsForEntity(frame, entity.entityId);
        if (!bounds) break;
        const SceneContainment containment = classifySceneContainment(*bounds, frame.worldSize);
        if (containment == SceneContainment::Inside) break;

        BeginMode2D(cam);
        const Rectangle box{bounds->x, bounds->y, bounds->width, bounds->height};
        DrawRectangleRec(box, Color{216, 180, 74, 32});
        DrawRectangleLinesEx(box, 2.f / cam.zoom, Color{216, 180, 74, 220});
        EndMode2D();
        break;
    }

    // Placeholder entities carry no artwork: a name above the box says what the
    // rectangle is instead of leaving an anonymous shape (UI audit 7.3).
    for (const SceneFrameEntity& entity : frame.entities) {
        if (hasVisibleSprite(frame, entity.entityId) || hasVisibleTilemapCells(frame, entity.entityId)) {
            continue;
        }
        if (entity.name.empty()) continue;
        const Vector2 top = GetWorldToScreen2D(
            Vector2{entity.bounds.x, entity.bounds.y}, cam);
        const Vector2 bottomRight = GetWorldToScreen2D(
            Vector2{entity.bounds.x + entity.bounds.width,
                    entity.bounds.y + entity.bounds.height}, cam);
        if (bottomRight.x - top.x < 18.f) continue;   // too small on screen to label
        // Dark chip behind the name so it reads on any world background colour.
        const float nameW = measureCanvasText(canvasFont, entity.name, 12.f);
        DrawRectangleRounded(
            Rectangle{top.x - 4.f, top.y - 20.f, nameW + 10.f, 17.f},
            0.35f, 4, Color{17, 17, 19, 200});
        drawCanvasText(canvasFont, entity.name, top.x + 1.f, top.y - 17.f, 12.f,
                       Color{212, 212, 216, 240});
    }

    // World size readout on the frame's bottom-right corner: the bounds in the
    // Inspector become visible in the workspace itself.
    {
        const Vector2 origin = GetWorldToScreen2D(Vector2{0.f, 0.f}, cam);
        drawCanvasText(canvasFont, "(0, 0)", origin.x + 6.f, origin.y + 6.f, 12.f,
                       Color{130, 130, 140, 170});
        const Vector2 corner = GetWorldToScreen2D(Vector2{world.x, world.y}, cam);
        const std::string dims =
            std::to_string(static_cast<int>(std::lround(world.x))) + " x "
            + std::to_string(static_cast<int>(std::lround(world.y))) + " wu";
        const float dimsW = measureCanvasText(canvasFont, dims, 12.f);
        drawCanvasText(canvasFont, dims, corner.x - dimsW, corner.y + 6.f, 12.f,
                       Color{130, 130, 140, 230});
    }

    const std::string& label = frame.sceneName;
    const float fontSize = 14.f;
    const float textW = measureCanvasText(canvasFont, label, fontSize);
    const Rectangle chip{static_cast<float>(rect.x) + 10.f, static_cast<float>(rect.y) + 8.f,
                         textW + 22.f, 25.f};
    DrawRectangleRounded(chip, 0.35f, 6, Color{17, 17, 19, 215});
    drawCanvasText(canvasFont, label, static_cast<float>(rect.x) + 21.f,
                   static_cast<float>(rect.y) + 13.f, fontSize, Color{96, 148, 240, 255});

    EndScissorMode();
}

} // namespace ArtCade::EditorNative
