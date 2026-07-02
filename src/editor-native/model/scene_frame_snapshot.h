#pragma once

#include "core/types.h"
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
    SceneFrameRect bounds;
    bool selected = false;
};

struct SceneFrameSprite {
    EntityId entityId = INVALID_ENTITY;
    AssetId assetId;
    SceneFrameRect destination;
    Vec2 origin;
    bool visible = false;
    bool selected = false;
    SceneFrameRect source;
    bool hasSource = false;
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
};

enum class SceneContainment {
    Inside,
    PartiallyOutside,
    FullyOutside,
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

// Topmost entity whose drawn representation contains the world point, or
// INVALID_ENTITY. A sprite occludes a placeholder; later draw order wins. Pure
// query on the frame — no document, no renderer.
EntityId pickEntityAt(const SceneFrameSnapshot& frame, Vec2 worldPoint);

} // namespace ArtCade::EditorNative
