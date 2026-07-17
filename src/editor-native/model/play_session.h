#pragma once

#include "core/types.h"
#include "logic-runtime.h"
#include "script-runtime.h"

#include <optional>
#include <memory>
#include <set>
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

// One populated tilemap cell in *local* cell coordinates - deliberately never
// world-space: the owning RuntimeEntity's Transform.position can change during
// Play (a mover), so the world destination is computed fresh every frame in
// collectSceneFrameSnapshot(const PlaySession&) via tilemapCellDestination(...),
// never baked in once at materialize time.
struct RuntimeTilemapCell {
    int cellX = 0;
    int cellY = 0;
    AnimationFrameRect sourceRect{};   // tileset pixel source rect
};

// Runtime copy of a TilemapComponent (ADR-0001: entity-owned), compiled once at
// materialize via resolveTilemapCellsStrict(...) - the same tile-id resolution
// Edit's tilemapRenderCells uses, just strict instead of lenient (Play must
// reject atomically on an unresolvable tile id, not skip it). No reference
// back to the authoring TilemapComponent or ProjectDocument. `cells` is
// already sparse (populated cells only); an entity with a TilemapComponent but
// no painted cells materializes an empty `cells` vector, which the Play
// snapshot collector treats as "draw nothing" - never the editor placeholder.
struct RuntimeTilemap {
    AssetId imageAssetId;
    Vec2 cellSize{32.f, 32.f};
    std::vector<RuntimeTilemapCell> cells;
};

struct RuntimeEntity {
    EntityId id = INVALID_ENTITY;
    ObjectTypeId objectTypeId;
    std::string name;
    Transform transform;
    // Runtime-only root visibility. Logic Board mutates this independently of
    // SpriteRenderer presence; Stop drops the entire runtime copy.
    bool visible = true;
    // Destroy is a runtime-only lifecycle state. The structural vector keeps
    // stable indices for render order while all simulation/render queries skip it.
    bool destroyed = false;
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
    EntityId owner = INVALID_ENTITY;
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
    // Edge-triggered Logic Board keys in deterministic registry order.
    std::vector<LogicKey> pressedLogicKeys;
    std::vector<LogicKey> releasedLogicKeys;
    std::vector<LogicKey> heldLogicKeys;
};

struct RuntimeScene {
    SceneId sourceSceneId;
    std::string name;
    Vec2 worldSize;
    Vec4 backgroundColor;
    std::vector<RuntimeEntity> entities;   // structural order (SceneDef::instances' own)
    // Back-to-front render order, as indices into `entities` - built once at
    // materialize from ProjectDocument::instancesInRenderOrder. A purely
    // visual draw-order hint for collectSceneFrameSnapshot(const PlaySession&);
    // advance()/update()/findEntity() always iterate `entities` directly in
    // its own structural order, so reordering scene layers can never change
    // simulation order.
    std::vector<std::size_t> renderOrder;
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

struct RuntimeAudioAsset {
    AssetId id;
    std::string sourcePath;
    AudioLoadMode loadMode = AudioLoadMode::StaticSound;
};

struct PlayAssetCatalogSnapshot {
    std::unordered_map<AssetId, RuntimeImageAsset> imageAssets;
    std::unordered_map<AssetId, RuntimeAudioAsset> audioAssets;
};

// One queued Play Sound request per Logic Board action invocation. Played back
// by the owner (EditorApp) via drainAudioCommands() — PlaySession itself never
// touches Raylib (see the class comment below).
struct RuntimeAudioCommand {
    enum class Type { PlayOneShot };

    Type type = Type::PlayOneShot;
    EntityId owner = INVALID_ENTITY;
    AssetId audioAssetId;
    float volume = 1.f;
};

// Runtime side of Play/Stop. It is built once from ProjectDocument at Start
// Play, then draw/tick read this session and never the authoring document.
class PlaySession {
public:
    ~PlaySession();
    PlaySession(PlaySession&& other) noexcept;
    PlaySession& operator=(PlaySession&& other) noexcept;
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;

    static std::optional<PlaySession> startProject(const ProjectDocument& document,
                                                   std::string* error = nullptr);
    static std::optional<PlaySession> startProject(
        const ProjectDocument& document,
        const std::vector<Scripts::ScriptProgram>& scripts,
        std::string* error = nullptr);

    static std::optional<PlaySession> startActiveScene(const ProjectDocument& document,
                                                       const SceneId& sceneId,
                                                       std::string* error = nullptr);
    static std::optional<PlaySession> startActiveScene(
        const ProjectDocument& document,
        const SceneId& sceneId,
        const std::vector<Scripts::ScriptProgram>& scripts,
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

    // Moves out every RuntimeAudioCommand queued by Play Sound actions since
    // the last drain. The caller (EditorApp) owns turning these into actual
    // playback via its own Raylib Sound cache — PlaySession stays free of
    // Raylib/RmlUi, same invariant as `update()` above.
    std::vector<RuntimeAudioCommand> drainAudioCommands();
    std::vector<Scripts::ScriptRuntimeDiagnostic> drainScriptDiagnostics();
    const Scripts::ScriptRuntime* scriptRuntime() const { return scriptRuntime_.get(); }

private:
    struct LogicHostAdapter;

    PlaySession() = default;
    static std::optional<PlaySession> materialize(const ProjectDocument& document,
                                                  const SceneId& sceneId,
                                                  const std::vector<Scripts::ScriptProgram>& scripts,
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
    RuntimeEntity* findEntityMutable(EntityId id);
    void refreshStaticCollider(EntityId owner);
    void dispatchCollisionTransitions();
    void flushPendingDestroys();
    bool playAnimationClip(EntityId id, const AssetId& animationAssetId,
                           const std::string& clipId);
    bool stopAnimation(EntityId id);
    bool setAnimationPlaybackSpeed(EntityId id, float speed);
    bool playSound(EntityId id, const AssetId& audioAssetId, float volume);
    bool setRuntimeStateNumber(const GameVariableId& id, double value);
    bool addRuntimeStateNumber(const GameVariableId& id, double delta);
    bool toggleRuntimeStateBoolean(const GameVariableId& id);
    std::optional<double> getRuntimeStateNumber(const GameVariableId& id) const;
    bool isLogicKeyHeld(LogicKey key) const;

    RuntimeScene scene_;
    PlayAssetCatalogSnapshot assets_;
    std::vector<RuntimeAudioCommand> pendingAudioCommands_;
    std::unordered_map<AssetId, RuntimeSpriteAnimationAsset> spriteAnimationAssets_;
    // Obstacle colliders frozen at materialize: enabled Solid / OneWayPlatform
    // colliders on non-movers (mover-vs-mover is out of scope). Trigger is
    // intentionally absent from resolution.
    std::vector<StaticRuntimeCollider> staticColliders_;
    // Heap-stable host/runtime: moving PlaySession into the coordinator never
    // invalidates the reference retained by LogicRuntime.
    std::unique_ptr<LogicHostAdapter> logicHost_;
    std::unique_ptr<Logic::LogicRuntime> logicRuntime_;
    std::unique_ptr<Scripts::ScriptRuntime> scriptRuntime_;
    std::unordered_map<EntityId, float> platformerMoveIntents_;
    std::unordered_map<EntityId, bool> platformerJumpIntents_;
    std::unordered_map<EntityId, Logic::ScopeToken> logicScopesByEntity_;
    std::set<std::pair<EntityId, EntityId>> activeCollisionPairs_;
    std::set<EntityId> pendingDestroy_;
    std::unordered_map<GameVariableId, GameVariableValue> runtimeVariables_;
    std::unordered_map<GameVariableId, GameVariableDefinition::Type> runtimeVariableTypes_;
    std::vector<LogicKey> heldLogicKeys_;
};

} // namespace ArtCade::EditorNative
