#pragma once

#include "core/types.h"
#include "editor-native/model/scene_frame_snapshot.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

struct RuntimeSpriteComponent {
    AssetId assetId;
    AnimationFrameRect sourceRect{};
    bool hasSourceRect = false;
    bool visible = true;
};

struct RuntimeSpriteAnimatorState {
    AssetId animationAssetId;
    std::string currentClipId;
    std::size_t currentFrameIndex = 0;
    float elapsedSeconds = 0.f;
    float playbackSpeed = 1.f;
    bool playing = false;
    bool finished = false;
};

// Input-driven top-down movement at a constant speed (the canonical maxSpeed).
struct RuntimeTopDownController {
    float speed = 0.f;
};

// Side-view platforming: horizontal input + gravity-driven vertical velocity,
// resolved against solids each frame. verticalVelocity and grounded are runtime-
// only: not persisted, recreated at Start Play, dropped at Stop. Convention:
// world +Y is down (gravity is positive, a jump sets a negative velocity).
struct RuntimePlatformerController {
    float moveSpeed = 0.f;
    float jumpSpeed = 0.f;
    float gravity   = 0.f;
    float verticalVelocity = 0.f;
    bool  grounded = false;
};

// Runtime copy of BoxCollider2D, materialized from the object type at Start Play.
struct RuntimeBoxCollider {
    Vec2 offset{};
    Vec2 size{32.f, 32.f};
    bool enabled = true;
    BoxColliderMode mode = BoxColliderMode::Solid;
};

// Runtime copy of a TilemapComponent (ADR-0001: entity-owned), compiled once at
// materialize via the same tilemapRenderCells(...) pure math Edit uses - no
// second cell->world/source-rect math, no reference back to the authoring
// TilemapComponent or ProjectDocument. `cells` is already sparse (populated
// cells only, per tilemapRenderCells); an entity with a TilemapComponent but
// no painted cells materializes an empty `cells` vector, which the Play
// snapshot collector treats as "draw nothing" - never the editor placeholder.
struct RuntimeTilemap {
    AssetId imageAssetId;
    std::vector<SceneFrameTilemapCell> cells;
};

struct RuntimeEntity {
    EntityId id = INVALID_ENTITY;
    std::string name;
    Transform transform;
    Vec2 velocity{};   // world units/second, resolved from authoring at materialize
    Vec3 fillColor{0.47f, 0.49f, 0.52f};
    std::optional<RuntimeSpriteComponent> sprite;
    std::optional<RuntimeSpriteAnimatorState> spriteAnimator;
    std::optional<RuntimeTopDownController> topDownController;
    std::optional<RuntimePlatformerController> platformerController;
    std::optional<RuntimeBoxCollider> collider;
    std::optional<RuntimeTilemap> tilemap;
};

// World-space axis-aligned bounding box (min/max corners).
struct Aabb {
    float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
};

// What a kinematic move actually did: the applied delta plus which sides hit a
// solid. The platformer derives grounded/ceiling from these (world +Y is down,
// so hitGround is a downward contact, hitCeiling an upward one).
struct KinematicMoveResult {
    Vec2 appliedDelta{};
    bool hitLeft = false;
    bool hitRight = false;
    bool hitCeiling = false;
    bool hitGround = false;
};

struct StaticRuntimeCollider {
    Aabb bounds;
    BoxColliderMode mode = BoxColliderMode::Solid;
};

// The single authoritative runtime collider AABB: center = position + offset,
// extents = size/2. Mirrors the editor's collider draw convention so physics and
// the overlay agree. Caller guarantees the entity has a collider.
Aabb runtimeColliderBounds(const RuntimeEntity& entity);

// Per-frame gameplay input, built by the application from the platform and fed to
// the session. PlaySession stays free of Raylib/RmlUi.
struct RuntimeInputSnapshot {
    bool moveLeft = false;
    bool moveRight = false;
    bool moveUp = false;
    bool moveDown = false;
    // Edge-triggered jump (the application computes it with IsKeyPressed, so the
    // session never sees Raylib). Consumed by the PlatformerController.
    bool jumpPressed = false;
};

struct RuntimeScene {
    SceneId sourceSceneId;
    std::string name;
    Vec2 worldSize;
    Vec4 backgroundColor;
    std::vector<RuntimeEntity> entities;
};

struct RuntimeImageAsset {
    AssetId id;
    std::string sourcePath;
};

struct RuntimeSpriteAnimationClip {
    std::string id;
    AssetId imageAssetId;     // the clip's own sheet
    std::vector<SpriteAnimationFrameDef> frames;
    float framesPerSecond = 8.f;
    AnimationPlaybackMode playbackMode = AnimationPlaybackMode::Loop;
};

struct RuntimeSpriteAnimationAsset {
    AssetId id;
    std::vector<RuntimeSpriteAnimationClip> clips;
};

struct PlayAssetCatalogSnapshot {
    std::unordered_map<AssetId, RuntimeImageAsset> imageAssets;
};

// Runtime side of Play/Stop. It is built once from ProjectDocument at Start
// Play, then draw/tick read this session and never the authoring document.
class PlaySession {
public:
    static std::optional<PlaySession> startProject(const ProjectDocument& document,
                                                   std::string* error = nullptr);

    static std::optional<PlaySession> startActiveScene(const ProjectDocument& document,
                                                       const SceneId& sceneId,
                                                       std::string* error = nullptr);

    const SceneId& sceneId() const { return scene_.sourceSceneId; }
    const RuntimeScene& scene() const { return scene_; }
    RuntimeScene& scene() { return scene_; }
    const PlayAssetCatalogSnapshot& assets() const { return assets_; }

    std::vector<RuntimeEntity>& entities() { return scene_.entities; }
    const std::vector<RuntimeEntity>& entities() const { return scene_.entities; }
    const RuntimeEntity* findEntity(EntityId id) const;

    // Runtime simulation step: integrates each entity's authored velocity into
    // its transform. Pure runtime mutation — never touches ProjectDocument.
    void advance(float dt);

    // Input-driven step: moves each TopDownController entity by the (diagonal-
    // normalized) input direction at its speed. Pure runtime mutation; opposite
    // inputs cancel; non-finite or non-positive dt is a no-op.
    void update(const RuntimeInputSnapshot& input, float dt);

private:
    static std::optional<PlaySession> materialize(const ProjectDocument& document,
                                                  const SceneId& sceneId,
                                                  std::string* error);

    // The one internal entry point for runtime movement: LinearMover (advance),
    // TopDownController and PlatformerController (update) route a desired delta
    // through here. It resolves the kinematic mover against the static solids with
    // a per-axis swept clamp (resolve X, then Y using the new X) so movement
    // slides along walls and never tunnels through thin ones at high speed. A
    // mover with no active solid collider moves freely. Returns the applied delta
    // plus the per-side contact flags the platformer needs.
    KinematicMoveResult moveKinematicEntity(RuntimeEntity& entity, Vec2 desiredDelta);

    // Per-entity input-driven movement, dispatched by `update` so a single entity
    // has exactly one movement writer.
    void updateTopDown(RuntimeEntity& entity, const RuntimeInputSnapshot& input, float dt);
    void updatePlatformer(RuntimeEntity& entity, const RuntimeInputSnapshot& input, float dt);

    RuntimeScene scene_;
    PlayAssetCatalogSnapshot assets_;
    std::unordered_map<AssetId, RuntimeSpriteAnimationAsset> spriteAnimationAssets_;
    // Obstacle colliders frozen at materialize: enabled Solid / OneWayPlatform
    // colliders on non-movers (mover-vs-mover is out of scope). Trigger is
    // intentionally absent from resolution.
    std::vector<StaticRuntimeCollider> staticColliders_;
};

} // namespace ArtCade::EditorNative
