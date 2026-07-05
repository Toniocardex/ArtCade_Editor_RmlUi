#include "editor-native/view/scene_view.h"

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

bool hasVisibleSprite(const SceneFrameSnapshot& frame, EntityId entityId) {
    for (const SceneFrameSprite& sprite : frame.sprites) {
        if (sprite.entityId == entityId && sprite.visible && !sprite.assetId.empty()) return true;
    }
    return false;
}

void drawMissingSprite(const SceneFrameSprite& sprite, float zoom) {
    const Rectangle bounds = toRectangle(sprite.destination);
    DrawRectangleRec(bounds, Color{70, 44, 58, 200});
    DrawRectangleLinesEx(bounds, 1.5f / zoom, Color{230, 90, 120, 230});
    DrawLineEx({bounds.x, bounds.y},
               {bounds.x + bounds.width, bounds.y + bounds.height},
               1.2f / zoom, Color{230, 90, 120, 230});
    DrawLineEx({bounds.x + bounds.width, bounds.y},
               {bounds.x, bounds.y + bounds.height},
               1.2f / zoom, Color{230, 90, 120, 230});
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
                       const ViewportRect& rect,
                       const TextureCache& textures) const {
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
    const SceneViewCamera vc = makeSceneViewCamera(rect, view, frame.worldSize);
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
        // at low zoom or very small cells.
        const SceneGridDefinition grid = makeSceneGridDefinition(view);
        const int visualStride = visualGridStrideForZoom(grid, cam.zoom);
        const float visualStep = grid.cellSize * static_cast<float>(visualStride);
        const Color gridMinor{120, 120, 130, 36};
        const Color gridMajor{120, 120, 130, 68};
        if (visualStep > 0.0f && std::isfinite(visualStep)) {
            const auto firstLine = [](float origin, float step) {
                return origin + std::ceil((0.0f - origin) / step) * step;
            };
            int ix = static_cast<int>(
                std::round((firstLine(grid.origin.x, visualStep) - grid.origin.x)
                           / grid.cellSize));
            for (float gx = firstLine(grid.origin.x, visualStep); gx <= world.x;
                 gx += visualStep, ix += visualStride) {
                DrawLineV({gx, 0.f}, {gx, world.y}, (ix % 4 == 0) ? gridMajor : gridMinor);
            }
            int iy = static_cast<int>(
                std::round((firstLine(grid.origin.y, visualStep) - grid.origin.y)
                           / grid.cellSize));
            for (float gy = firstLine(grid.origin.y, visualStep); gy <= world.y;
                 gy += visualStep, iy += visualStride) {
                DrawLineV({0.f, gy}, {world.x, gy}, (iy % 4 == 0) ? gridMajor : gridMinor);
            }
        }
    }

    // Neutral world frame — clearly marks where the world ends, while the accent
    // selection still stands out against it.
    const float linePx = 2.f / cam.zoom;
    DrawRectangleLinesEx(Rectangle{0, 0, world.x, world.y}, linePx, Color{92, 92, 102, 255});

    for (const SceneFrameEntity& entity : frame.entities) {
        if (hasVisibleSprite(frame, entity.entityId)) continue;
        const Rectangle box = toRectangle(entity.bounds);
        DrawRectangleRec(box, toColor(entity.fillColor, 0.92f));
        DrawRectangleLinesEx(box, 1.f / cam.zoom, Color{12, 14, 18, 200});
    }

    for (const SceneFrameSprite& sprite : frame.sprites) {
        if (!sprite.visible) continue;
        const TextureResource* resource = textures.find(sprite.assetId);
        if (!resource || !resource->loaded) {
            drawMissingSprite(sprite, cam.zoom);
            continue;
        }

        const Rectangle source = sprite.hasSource
            ? toRectangle(sprite.source)
            : Rectangle{0.f, 0.f,
                        static_cast<float>(resource->texture.width),
                        static_cast<float>(resource->texture.height)};
        DrawTexturePro(resource->texture, source, toRectangle(sprite.destination),
                       Vector2{0.f, 0.f}, 0.f, WHITE);
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
            const std::optional<WorldRect> editorBounds =
                editorBoundsForEntity(frame, entity.entityId);
            const Rectangle box = editorBounds
                ? Rectangle{editorBounds->x, editorBounds->y,
                            editorBounds->width, editorBounds->height}
                : toRectangle(entity.bounds);
            const Rectangle sel{box.x - 3.f, box.y - 3.f, box.width + 6.f, box.height + 6.f};
            DrawRectangleLinesEx(sel, 2.f / cam.zoom, Color{59, 130, 246, 255});
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
        if (hasVisibleSprite(frame, entity.entityId)) continue;
        if (entity.name.empty()) continue;
        const Vector2 top = GetWorldToScreen2D(
            Vector2{entity.bounds.x, entity.bounds.y}, cam);
        const Vector2 bottomRight = GetWorldToScreen2D(
            Vector2{entity.bounds.x + entity.bounds.width,
                    entity.bounds.y + entity.bounds.height}, cam);
        if (bottomRight.x - top.x < 18.f) continue;   // too small on screen to label
        // Dark chip behind the name so it reads on any world background colour.
        const int nameW = MeasureText(entity.name.c_str(), 12);
        DrawRectangleRounded(
            Rectangle{top.x - 4.f, top.y - 20.f, static_cast<float>(nameW) + 10.f, 17.f},
            0.35f, 4, Color{17, 17, 19, 200});
        DrawText(entity.name.c_str(), static_cast<int>(top.x) + 1,
                 static_cast<int>(top.y) - 17, 12, Color{212, 212, 216, 240});
    }

    // World size readout on the frame's bottom-right corner: the bounds in the
    // Inspector become visible in the workspace itself.
    {
        const Vector2 corner = GetWorldToScreen2D(Vector2{world.x, world.y}, cam);
        const std::string dims =
            std::to_string(static_cast<int>(std::lround(world.x))) + " x "
            + std::to_string(static_cast<int>(std::lround(world.y))) + " wu";
        const int dimsW = MeasureText(dims.c_str(), 12);
        DrawText(dims.c_str(), static_cast<int>(corner.x) - dimsW,
                 static_cast<int>(corner.y) + 6, 12, Color{130, 130, 140, 230});
    }

    const char* label = frame.sceneName.c_str();
    const int fontSize = 14;
    const float textW = static_cast<float>(MeasureText(label, fontSize));
    const Rectangle chip{static_cast<float>(rect.x) + 10.f, static_cast<float>(rect.y) + 8.f,
                         textW + 22.f, 25.f};
    DrawRectangleRounded(chip, 0.35f, 6, Color{17, 17, 19, 215});
    DrawText(label, rect.x + 21, rect.y + 13, fontSize, Color{96, 148, 240, 255});

    EndScissorMode();
}

} // namespace ArtCade::EditorNative
