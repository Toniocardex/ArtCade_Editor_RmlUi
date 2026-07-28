#pragma once

#include "core/types.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/box_collider_view.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;
class PlaySession;

struct SceneFrameRect {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
};

struct SceneFrameEntity {
    EntityId entityId = INVALID_ENTITY;
    std::string name;
    Vec3 fillColor;
    // Unrotated visual rect (top-left + size). Use with rotationRadians for
    // DrawRectanglePro / OBB pick; AABB for containment is derived when needed.
    SceneFrameRect bounds;
    bool selected = false;
    float rotationRadians = 0.f;
};

struct SceneFrameSprite {
    EntityId entityId = INVALID_ENTITY;
    AssetId assetId;
    SceneFrameRect destination;   // unrotated destination rect
    Vec2 origin;                  // DrawTexturePro origin (typically half size)
    bool visible = false;
    bool selected = false;
    SceneFrameRect source;
    bool hasSource = false;
    float rotationRadians = 0.f;
    /** Runtime/authored horizontal mirror (raylib: negative source width). */
    bool flipX = false;
    /** Runtime/authored vertical mirror (raylib: negative source height). */
    bool flipY = false;
};

struct SceneFrameTilemapCell {
    SceneFrameRect destination;   // world rect
    SceneFrameRect source;        // pixel rect within the tileset's image
};

struct SceneFrameTilemap {
    EntityId entityId = INVALID_ENTITY;
    AssetId imageAssetId;   // the tileset's underlying image (TextureCache key)
    std::vector<SceneFrameTilemapCell> cells;   // one per populated cell; empty if unpainted
    bool selected = false;
};

struct SceneFrameText {
    EntityId entityId = INVALID_ENTITY;
    std::string displayText;
    Vec2 anchorPosition;
    std::string align = "top-left";
    int size = 24;
    Vec4 color{1.f, 1.f, 1.f, 1.f};
    bool screenSpace = false;
    float layerOpacity = 1.f;
    // Project-relative FontAssetDef.sourcePath; empty = the Scene View's
    // default CanvasFont (ADR-0036). Play/export resolve the same field
    // (TextComponent::fontPath) independently through Modules::FontCache.
    std::string fontPath;
};

struct SceneFrameGauge {
    EntityId entityId = INVALID_ENTITY;
    Vec2 anchorPosition;
    float width = 64.f;
    float height = 8.f;
    Vec4 fillColor{0.23f, 0.82f, 0.23f, 1.f};
    Vec4 bgColor{0.13f, 0.13f, 0.13f, 1.f};
    float ratio = 1.f;
    std::string direction = "horizontal";
    bool screenSpace = false;
};

struct SceneFrameSnapshot {
    SceneId sceneId;
    std::string sceneName;
    Vec2 worldSize;
    Vec4 backgroundColor;
    bool hasScene = false;
    std::vector<SceneFrameEntity> entities;
    std::vector<SceneFrameSprite> sprites;
    std::vector<SceneFrameCollider> colliders;
    std::vector<SceneFrameTilemap> tilemaps;
    std::vector<SceneFrameText> texts;
    std::vector<SceneFrameGauge> gauges;
};

enum class SceneContainment {
    Inside,
    PartiallyOutside,
    FullyOutside,
};

/** World + viewport pointers for mixed world/HUD picking. */
struct ScenePickPoint {
    Vec2 world;
    Vec2 viewport;
};

// Edit projection. Entities/sprites are emitted in per-scene layer order
// (scene.layers, index 0 = background, last = foreground), so the renderer draws
// back-to-front and reverse-iterating picks the topmost. Layers in @p hiddenLayers
// are skipped entirely (not drawn, not pickable). A scene without layers (legacy)
// keeps its instance order. The renderer never reads scene.layers itself.
SceneFrameSnapshot collectSceneFrameSnapshot(const ProjectDocument& document,
                                             const SceneId& sceneId,
                                             EntityId selectedEntity,
                                             const std::unordered_set<std::string>& hiddenLayers = {});
SceneFrameSnapshot collectSceneFrameSnapshot(const PlaySession& session);

// Derived editor geometry for selection, recovery, and containment warnings.
// Uses the frame projection only: visible sprite bounds and collider bounds are
// unioned when present; otherwise the editorial placeholder bounds are used.
std::optional<WorldRect> editorBoundsForEntity(const SceneFrameSnapshot& frame,
                                               EntityId entityId);
SceneContainment classifySceneContainment(const WorldRect& entityBounds,
                                          Vec2 sceneSize);
std::optional<Vec2> positionToBringBoundsInsideScene(const WorldRect& entityBounds,
                                                     Vec2 currentPosition,
                                                     Vec2 sceneSize);

// Topmost entity whose drawn representation contains the point, or
// INVALID_ENTITY. World visuals use @p point.world; HUD (screenSpace) texts
// and gauges use @p point.viewport. Pure query on the frame — no document.
EntityId pickEntityAt(const SceneFrameSnapshot& frame, ScenePickPoint point);
/** Convenience: world-only pick (HUD texts/gauges ignored). */
EntityId pickEntityAt(const SceneFrameSnapshot& frame, Vec2 worldPoint);

// In-place local drag preview: offsets every drawn representation of
// `entity` by `delta` (world units) so the Scene View shows the move before
// the single SetEntityTransformCommand lands on release. Must cover every
// snapshot list that carries its own world-space rect independent of
// SceneFrameEntity::bounds - a list left out here stays frozen at the
// pre-drag position for the whole gesture and only jumps on release, which
// reads as stutter even though frame time itself is unaffected. Workspace-
// only: never touches ProjectDocument, revision, or dirty.
void applyDragPreviewOffset(SceneFrameSnapshot& snapshot, EntityId entity, Vec2 delta);

} // namespace ArtCade::EditorNative
