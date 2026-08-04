#include "editor-native/view/scene_view.h"

#include "editor-native/model/authored_transform.h"
#include "editor-native/model/tilemap_render_view.h"
#include "editor-native/view/editor_font_cache.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/text_visual_layout.h"
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

// An "Entity Visible"-off instance still draws in Edit mode, dimmed, rather
// than disappearing (it must stay selectable while editing) - it is only
// truly invisible in an actual running game. Matches
// scene_entities_pass.cpp's own inEditMode-dims/gameplay-hides constant.
constexpr float kInvisibleInGameDimAlpha = 0.45f;

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

// ADR-0036: a Text component's own configured font when resolved, else the
// Scene View's default CanvasFont (Inter) — matching what an empty fontPath,
// a missing font asset, or a load failure all already fall back to today.
const Font& resolveTextFont(const EditorFontCache& fonts, const CanvasFont& canvasFont,
                            const std::string& fontPath) {
    if (!fontPath.empty()) {
        if (const FontResource* resource = fonts.find(fontPath); resource && resource->loaded) {
            return resource->font;
        }
    }
    return canvasFont.loaded ? canvasFont.font : GetFontDefault();
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

bool hasTextVisual(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameText& text : frame.texts) {
        if (text.entityId == entityId && !text.displayText.empty()) return true;
    }
    return false;
}

bool hasGaugeVisual(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameGauge& gauge : frame.gauges) {
        if (gauge.entityId == entityId && gauge.width > 0.f && gauge.height > 0.f) return true;
    }
    return false;
}

bool hasColliderVisual(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameCollider& collider : frame.colliders) {
        if (collider.entityId == entityId && collider.enabled) return true;
    }
    return false;
}

bool hasPresentationVisual(const SceneFrameSnapshot& frame, EntityId entityId) {
    return hasVisibleSprite(frame, entityId)
        || hasVisibleTilemapCells(frame, entityId)
        || hasColliderVisual(frame, entityId)
        || hasTextVisual(frame, entityId)
        || hasGaugeVisual(frame, entityId);
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
    const SceneFrameTransform2D& xf = sprite.visualTransform;
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
                       const CanvasFont& canvasFont,
                       const EditorFontCache& fonts,
                       const TransformGizmoOverlay* gizmo) const {
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
        const bool hasText = hasTextVisual(frame, entity.entityId);
        const bool hasGauge = hasGaugeVisual(frame, entity.entityId);
        const bool hasCollider = hasColliderVisual(frame, entity.entityId);
        const unsigned char dimAlpha = entity.visibleInGame
            ? static_cast<unsigned char>(255)
            : static_cast<unsigned char>(255.f * kInvisibleInGameDimAlpha);
        const Color tint{255, 255, 255, dimAlpha};
        if (!hasSprite && !hasTilemap && !hasCollider && !hasText && !hasGauge) {
            const SceneFrameTransform2D xf = visualOf(entity.bounds, entity.rotationRadians);
            const float degrees = entity.rotationRadians * kRadToDeg;
            const Vector2 origin{entity.bounds.width * 0.5f, entity.bounds.height * 0.5f};
            Color fill = toColor(entity.fillColor, 0.92f);
            fill.a = static_cast<unsigned char>(fill.a * (dimAlpha / 255.f));
            DrawRectanglePro(pivotDestination(entity.bounds, origin), origin, degrees, fill);
            drawOrientedOutline(xf, 1.f / cam.zoom,
                                Color{12, 14, 18, static_cast<unsigned char>(200 * (dimAlpha / 255.f))});
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
                                      toRectangle(cell.destination), Vector2{0.f, 0.f}, 0.f, tint);
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
                    Rectangle source = sprite.hasSource
                        ? toRectangle(sprite.source)
                        : Rectangle{0.f, 0.f,
                                    static_cast<float>(resource->texture.width),
                                    static_cast<float>(resource->texture.height)};
                    // Match runtime renderer_draw: negative source size flips.
                    if (sprite.flipX) source.width = -source.width;
                    if (sprite.flipY) source.height = -source.height;
                    const Vector2 origin{sprite.origin.x, sprite.origin.y};
                    DrawTexturePro(resource->texture, source,
                                  pivotDestination(sprite.destination, origin), origin,
                                  sprite.rotationRadians * kRadToDeg, tint);
                }
                break;   // one SpriteRenderer per entity
            }
        }
        for (const SceneFrameGauge& gauge : frame.gauges) {
            if (gauge.entityId != entity.entityId || gauge.screenSpace) continue;
            const Rectangle bg{
                gauge.anchorPosition.x, gauge.anchorPosition.y, gauge.width, gauge.height};
            const float dim = gauge.visibleInGame ? 1.f : kInvisibleInGameDimAlpha;
            Vec4 bgColor = gauge.bgColor;
            bgColor.a *= dim;
            Vec4 fillColor = gauge.fillColor;
            fillColor.a *= dim;
            DrawRectangleRec(bg, toColor(bgColor));
            if (gauge.direction == "vertical") {
                const float fh = gauge.height * gauge.ratio;
                DrawRectangleRec(
                    Rectangle{bg.x, bg.y + (gauge.height - fh), gauge.width, fh},
                    toColor(fillColor));
            } else {
                DrawRectangleRec(
                    Rectangle{bg.x, bg.y, gauge.width * gauge.ratio, gauge.height},
                    toColor(fillColor));
            }
        }
        for (const SceneFrameText& text : frame.texts) {
            if (text.entityId != entity.entityId || text.screenSpace) continue;
            const Font& glyphFont = resolveTextFont(fonts, canvasFont, text.fontPath);
            const TextVisualLayout layout = layoutSceneFrameText(text, glyphFont);
            Vec4 color = text.color;
            color.a *= text.layerOpacity;
            if (!text.visibleInGame) color.a *= kInvisibleInGameDimAlpha;
            drawCanvasText(glyphFont, text.displayText, layout.drawPosition.x,
                           layout.drawPosition.y, static_cast<float>(text.size), toColor(color));
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
                outline = sprite.visualTransform;
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
                    matchedContent = true;
                }
            }
            if (!matchedContent) {
                for (const SceneFrameCollider& collider : frame.colliders) {
                    if (collider.entityId != entity.entityId || !collider.enabled) continue;
                    const WorldRect& b = collider.worldBounds;
                    outline = SceneFrameTransform2D{
                        {b.x + b.width * 0.5f, b.y + b.height * 0.5f},
                        {b.width, b.height}, 0.f};
                    matchedContent = true;
                    break;
                }
            }
            if (!matchedContent) {
                std::optional<SceneFrameRect> textGaugeBounds;
                const auto addBounds = [&](SceneFrameRect value) {
                    if (!textGaugeBounds) { textGaugeBounds = value; return; }
                    const float x0 = std::min(textGaugeBounds->x, value.x);
                    const float y0 = std::min(textGaugeBounds->y, value.y);
                    const float x1 = std::max(textGaugeBounds->x + textGaugeBounds->width,
                                              value.x + value.width);
                    const float y1 = std::max(textGaugeBounds->y + textGaugeBounds->height,
                                              value.y + value.height);
                    textGaugeBounds = {x0, y0, x1 - x0, y1 - y0};
                };
                for (const SceneFrameText& text : frame.texts) {
                    if (text.entityId != entity.entityId || text.screenSpace
                        || text.displayText.empty()) continue;
                    const Font& glyphFont = resolveTextFont(fonts, canvasFont, text.fontPath);
                    addBounds(layoutSceneFrameText(text, glyphFont).bounds);
                }
                for (const SceneFrameGauge& gauge : frame.gauges) {
                    if (gauge.entityId != entity.entityId || gauge.screenSpace
                        || gauge.width <= 0.f || gauge.height <= 0.f) continue;
                    addBounds({gauge.anchorPosition.x, gauge.anchorPosition.y,
                               gauge.width, gauge.height});
                }
                if (textGaugeBounds) outline = visualOf(*textGaugeBounds, 0.f);
            }
            // Inflate slightly in local space for a readable pad around the visual.
            outline.size.x += 6.f;
            outline.size.y += 6.f;
            drawOrientedOutline(outline, 2.f / cam.zoom, Color{59, 130, 246, 255});

            // ADR-0058: every selected Edit instance exposes the sole Entity Origin.
            {
                const Vector2 pivotWorld{entity.entityOrigin.x, entity.entityOrigin.y};
                const float r = 3.5f / cam.zoom;
                DrawCircleV(pivotWorld, r, Color{250, 204, 21, 230});
                DrawLineEx({pivotWorld.x - r * 2.f, pivotWorld.y},
                           {pivotWorld.x + r * 2.f, pivotWorld.y},
                           1.2f / cam.zoom, Color{250, 204, 21, 200});
                DrawLineEx({pivotWorld.x, pivotWorld.y - r * 2.f},
                           {pivotWorld.x, pivotWorld.y + r * 2.f},
                           1.2f / cam.zoom, Color{250, 204, 21, 200});
            }
        }
    }

    if (gizmo && gizmo->visible) {
        const SceneFrameTransform2D& g = gizmo->geometry;
        drawOrientedOutline(g, 1.5f / cam.zoom, Color{96, 165, 250, 220});

        if (gizmo->showScaleHandles) {
            static constexpr TransformHandle kHandles[] = {
                TransformHandle::CornerTL, TransformHandle::CornerTR,
                TransformHandle::CornerBR, TransformHandle::CornerBL,
                TransformHandle::EdgeT, TransformHandle::EdgeR,
                TransformHandle::EdgeB, TransformHandle::EdgeL,
            };
            const float visual = transformHandleWorldExtent(cam.zoom, kTransformHandleVisualPx);
            for (TransformHandle h : kHandles) {
                const std::optional<Vec2> pos = transformHandleWorldPosition(g, h);
                if (!pos) continue;
                const bool hot = h == gizmo->hovered || h == gizmo->active;
                const Color fill = hot ? Color{255, 255, 255, 255} : Color{226, 232, 240, 255};
                const Color border = hot ? Color{37, 99, 235, 255} : Color{59, 130, 246, 255};
                const Rectangle r{
                    pos->x - visual * 0.5f,
                    pos->y - visual * 0.5f,
                    visual,
                    visual,
                };
                DrawRectangleRec(r, fill);
                DrawRectangleLinesEx(r, std::max(1.f, 1.f / cam.zoom), border);
            }
        }
    }

    EndMode2D();
    }

    if (gizmo && gizmo->visible && gizmo->showReadout) {
        const Vec2 size{
            gizmo->unscaledSize.x * gizmo->previewTransform.scale.x,
            gizmo->unscaledSize.y * gizmo->previewTransform.scale.y,
        };
        const std::string line1 =
            "Scale " + formatAuthoringFloat(gizmo->previewTransform.scale.x) + " x "
            + formatAuthoringFloat(gizmo->previewTransform.scale.y);
        const std::string line2 =
            "Size " + formatAuthoringFloat(size.x) + " x " + formatAuthoringFloat(size.y) + " wu";
        const float x = gizmo->readoutScreen.x + 14.f;
        const float y = gizmo->readoutScreen.y + 14.f;
        drawCanvasText(canvasFont, line1, x, y, 14.f, Color{226, 232, 240, 255});
        drawCanvasText(canvasFont, line2, x, y + 16.f, 14.f, Color{148, 163, 184, 255});
    }

    // HUD (screenSpace) text/gauge — viewport space, after world pass.
    BeginScissorMode(rect.x, rect.y, rect.width, rect.height);
    for (const SceneFrameGauge& gauge : frame.gauges) {
        if (!gauge.screenSpace) continue;
        const float x = static_cast<float>(rect.x) + gauge.anchorPosition.x;
        const float y = static_cast<float>(rect.y) + gauge.anchorPosition.y;
        const float dim = gauge.visibleInGame ? 1.f : kInvisibleInGameDimAlpha;
        Vec4 bgColor = gauge.bgColor;
        bgColor.a *= dim;
        Vec4 fillColor = gauge.fillColor;
        fillColor.a *= dim;
        DrawRectangleRec(Rectangle{x, y, gauge.width, gauge.height}, toColor(bgColor));
        if (gauge.direction == "vertical") {
            const float fh = gauge.height * gauge.ratio;
            DrawRectangleRec(Rectangle{x, y + (gauge.height - fh), gauge.width, fh},
                             toColor(fillColor));
        } else {
            DrawRectangleRec(Rectangle{x, y, gauge.width * gauge.ratio, gauge.height},
                             toColor(fillColor));
        }
    }
    for (const SceneFrameText& text : frame.texts) {
        if (!text.screenSpace) continue;
        SceneFrameText viewportText = text;
        viewportText.anchorPosition = Vec2{
            static_cast<float>(rect.x) + text.anchorPosition.x,
            static_cast<float>(rect.y) + text.anchorPosition.y,
        };
        const Font& glyphFont = resolveTextFont(fonts, canvasFont, text.fontPath);
        const TextVisualLayout layout = layoutSceneFrameText(viewportText, glyphFont);
        Vec4 color = text.color;
        color.a *= text.layerOpacity;
        if (!text.visibleInGame) color.a *= kInvisibleInGameDimAlpha;
        drawCanvasText(glyphFont, text.displayText, layout.drawPosition.x,
                       layout.drawPosition.y, static_cast<float>(text.size), toColor(color));
    }

    // The scene-name chip is a viewport-space overlay, not clipped to the scene.
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
        if (hasPresentationVisual(frame, entity.entityId)) {
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
