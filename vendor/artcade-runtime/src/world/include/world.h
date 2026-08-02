#pragma once

#include "../../core/types.h"
#include "../../modules/collision/include/collision_world.h"
#include "runtime_identity.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ArtCade::Modules {
    class RuntimeEntityGateway;
    class SceneLifecycleService;
    class Physics;
    class VariableManager;
    class Renderer;
    class SpriteAnimator;
}

namespace ArtCade {

class World;
struct PlatformerControllerComponent;
struct TopDownControllerComponent;

/** ADR-0022: near-flush contact recognition / correction (not resting offset). */
inline constexpr float kGroundContactSkin = 1.0f;
/** ADR-0022: One Way “came from above” eligibility only. */
inline constexpr float kOneWayApproachTolerance = 2.0f;
/** Legacy inclusive epsilon; ADR-0052 support uses scaled minHorizontalOverlap. */
inline constexpr float kGroundHorizontalOverlapEpsilon = 0.01f;

struct GroundSupport {
    EntityId supportEntityId = INVALID_ENTITY;
    std::size_t selfShapeIndex = 0;
    std::size_t supportShapeIndex = 0;
    float supportTopY = 0.f;
    float correctionY = 0.f;
    bool oneWay = false;
    /** ADR-0052 diagnostics / deterministic ranking. */
    float horizontalOverlap = 0.f;
    float feetBottomY = 0.f;
};

/** ADR-0052: derived thresholds for one findGroundSupport evaluation. */
struct GroundSupportPolicy {
    float contactSkin = kGroundContactSkin;
    float minHorizontalOverlap = 0.f;
    float bodyInsetX = 0.f;
    float maxFloorSnapDistance = 0.f;
};

struct KinematicCollisionResult {
    bool grounded = false;
    EntityId groundEntityId = INVALID_ENTITY;
};

/** ADR-0052 / ADR-0054: Platformer X-phase wall contacts (transient). */
struct PlatformerXMoveResult {
    bool blocked = false;
    bool hitLeftWall = false;  // dx < 0
    bool hitRightWall = false; // dx > 0
    EntityId contactEntityId = INVALID_ENTITY;
};

/** ADR-0053 / ADR-0054: transient Y-phase contacts (never compete with PlatformerRt.grounded). */
struct PlatformerYMoveResult {
    bool hitFloor = false;
    bool hitCeiling = false;
    EntityId contactEntityId = INVALID_ENTITY;
};

/** ADR-0053 / ADR-0054: last-step diagnostics for tests (not cross-frame authority). */
struct PlatformerStepContacts {
    bool hitFloor = false;
    bool hitCeiling = false;
    EntityId supportEntityId = INVALID_ENTITY;
};

struct WallJumpIntent {
    bool pending = false;
    PlatformerWallSide side = PlatformerWallSide::None;
    float horizontalSpeed = 0.f;
    float verticalSpeed = 0.f;
    EntityId wallEntityId = INVALID_ENTITY;
};

struct WallSlideIntent {
    bool pending = false;
    float maxFallSpeed = 0.f;
    PlatformerWallSide side = PlatformerWallSide::None;
};

/** ADR-0053: collisional orthogonal overlap slop (not support threshold). */
inline float platformerContactSlop(float orthogonalExtent) {
    if (!std::isfinite(orthogonalExtent) || orthogonalExtent <= 0.f)
        return std::numeric_limits<float>::infinity(); // fail closed
    return std::clamp(orthogonalExtent * 0.001f, 0.01f, 0.25f);
}

GroundSupportPolicy groundSupportPolicyFor(float supportWidth, float supportHeight);

namespace WorldInternal {
void stepPlatformerController(World& world,
                              EntityId id,
                              const PlatformerControllerComponent& pc,
                              float dt);
void stepTopDownController(World& world,
                           EntityId id,
                           const TopDownControllerComponent& tc,
                           float dt);
} // namespace WorldInternal

/** Per-axis Game View clamp (ADR-0018). Non-finite / non-positive extents → 0. */
float clampCameraAxis(float worldExtent, float viewportExtent, float requestedCenter);
Vec2 clampCameraCenter(Vec2 worldSize, Vec2 viewportSize, Vec2 center);

struct ResolvedCameraTarget {
    EntityId id = INVALID_ENTITY;
    Vec2 desiredCenter{};
    float followSpeed = 0.f;
};

/**
 * World — game-state orchestrator (Layer 3).
 *
 * Global blackboard: VariableManager (Lua state.* / save.*).
 * Scene activation: SceneLifecycleService (when wired) or gateway fallback.
 */
class World {
public:
    World(Modules::RuntimeEntityGateway& entityGateway,
          Modules::Physics&              physics,
          Modules::VariableManager&      variables);

    /** Non-owning; set by Application so loadScene routes through lifecycle. */
    void setSceneLifecycleService(Modules::SceneLifecycleService* lifecycle);

    void setRenderer(Modules::Renderer* renderer);
    void setSpriteAnimator(Modules::SpriteAnimator* animator);
    /** Called for an explicit gameplay destroy while the entity is still live. */
    void setEntityWillDestroyHandler(std::function<void(EntityId)> handler);
    void setEntityDestroyedHandler(std::function<void(EntityId)> handler);

    void init(const ProjectDoc& doc);
    /** After editor_load_project: refresh tile collisions + gameplay runtime maps. */
    void syncAfterEditorProject(const std::vector<TilePaletteEntry>& tilePalette);
    /** Preview STOP: reset global state + gameplay maps without reloading Lua. */
    void restoreDesignState(const std::vector<TilePaletteEntry>& tilePalette);
    void shutdown();

    /** Clears per-scene gameplay caches after SceneLifecycleService commits a load. */
    void onSceneActivated();

    /**
     * Snap camera for the active scene (ADR-0018): Camera Target + offset, else
     * cameraStart + viewport/2, then clamp. Call from init and onSceneActivated.
     */
    void resetCameraForActiveScene();

    /**
     * Lowest-id active CameraTarget (Automatic), or explicit follow target.
     * Shared by reset (snap) and tick (smooth).
     */
    std::optional<ResolvedCameraTarget> resolveCameraTarget() const;

    bool    loadScene(const SceneId& id);
    SceneId activeSceneId() const;

    void syncPhysicsToEntities();
    void tickGameplaySystems(float dt);
    /** Platformer owns Transform; runs before physics.step. Kinematic collider
     *  bodies are pushed from Transform each tick — not pulled by syncPhysics. */
    void tickPlatformerControllers(float dt);
    /** Refresh collision enter/stay/exit contacts after physics updates. */
    void refreshCollisionEvents();
    /** Follow exactly one camera target. Automatic mode selects the lowest active id. */
    void tickCameraTargets(float dt);
    /** Override CameraTargetComponent selection until stop/useAutomatic is called. */
    bool followCameraTarget(EntityId id);
    /** Disable automatic and explicit camera following without moving the camera. */
    void stopCameraFollow();
    /** Return to deterministic CameraTargetComponent selection. */
    void useAutomaticCameraTarget();
    /** Runtime camera position, available even for headless/editor presentation. */
    Vec2 cameraCenter() const { return cameraCenter_; }
    /** Count down AutoDestroy lifespans and queue destroys (call before flush). */
    void tickAutoDestroy(float dt);
    void flushEntityQueues();

    bool       hasGlobalState(const std::string& key) const;
    StateValue getGlobalState(const std::string& key) const;
    void       setGlobalState(const std::string& key, const StateValue& value);

    std::vector<EntityId> activeEntityIds() const;

    void rebuildCollisionWorld();
    bool collisionOverlap(EntityId a, EntityId b) const;
    EntityId firstCollisionTouching(EntityId id, const CollisionWorld::Filter& filter) const;
    /** Number of active runtime collision shapes, useful for debug overlays/tests. */
    size_t collisionShapeCount() const;
    /** Read-only runtime collision shapes generated from entities and tilemaps. */
    const std::vector<CollisionWorld::ShapeRef>& collisionShapes() const;
    /** Read-only collision events computed during the latest collision refresh. */
    const std::vector<CollisionWorld::ContactEvent>& collisionEvents() const;
    /** Return current-frame collision events involving id, normalized so id is self. */
    std::vector<CollisionWorld::ContactEvent> collisionEventsFor(
        EntityId id,
        const std::string& kind,
        const CollisionWorld::Filter& filter) const;
    /** Fast predicate for event gates such as Logic Board enter/exit triggers. */
    bool hasCollisionEvent(
        EntityId id,
        const std::string& kind,
        const CollisionWorld::Filter& filter) const;
    CollisionWorld::RaycastResult collisionRaycast(
        const Vec2& from,
        const Vec2& to,
        const CollisionWorld::Filter& filter = {}) const;
    bool collisionGrounded(EntityId id) const;
    /** ADR-0022: same support path as kinematic resolve; One Way respects velocity. */
    bool collisionGrounded(EntityId id, float verticalVelocity) const;
    KinematicCollisionResult resolveKinematicCollisionBody(
        EntityId id,
        Transform& transform,
        const Transform& beforeMove,
        float& horizontalVelocity,
        float& verticalVelocity) const;

    void snapEntityToGrid(EntityId id, float cellSize);
    void moveEntityByOffset(EntityId id, float dx, float dy);
    bool isSpaceFree(float x, float y, float w, float h) const;
    /** True when entity has PlatformerController and its feet touch solid collision. */
    bool isPlatformerGrounded(EntityId id) const;
    /**
     * True when airborne (not grounded, not climbing) and vertical velocity is
     * downward (+Y down). Rising after a jump is false.
     */
    bool isPlatformerFalling(EntityId id) const;
    /**
     * True when platformerState is Moving only (Climbing is not Moving).
     * Prefer platformerState() for new code.
     */
    bool isPlatformerMovingHorizontally(EntityId id) const;
    /**
     * Mutually exclusive locomotion (ADR-0016 / ADR-0052): Stopped/Moving
     * grounded, Climbing on ladders, WallSliding while wall-slide intent is
     * active with a matching current X block, Jumping/Falling airborne
     * (apex via lastAirState).
     */
    PlatformerState platformerState(EntityId id) const;

    /** ADR-0053: last fixed-step contact diagnostics (tests / debug). */
    PlatformerStepContacts lastPlatformerStepContacts(EntityId id) const;

    /** ADR-0055: last completed Platformer contact projection (Logic / tests). */
    PlatformerContactProjection platformerContactProjection(EntityId id) const;

    /**
     * ADR-0055: queue wall jump for the next fixed step (side captured at request).
     * Survives clearFrameMovementIntents.
     */
    bool requestWallJump(EntityId id, PlatformerWallSide side,
                         float horizontalSpeed, float verticalSpeed);
    /** ADR-0055: queue wall-slide fall clamp for the next fixed step. */
    bool requestWallSlide(EntityId id, PlatformerWallSide side, float maxFallSpeed);

    /** Canonical Logic Runtime operations over materialized world state. */
    bool isActiveEntity(EntityId id) const;
    bool isObjectType(EntityId id, const ObjectTypeId& expected) const;
    bool requestDestroy(EntityId id);
    bool playAnimationClip(EntityId id, const AssetId& animationAssetId,
                           const std::string& clipId);
    bool stopAnimation(EntityId id);
    bool setAnimationPlaybackSpeed(EntityId id, float speed);

    void setMovementIntent(EntityId id, float directionX, float directionY);
    void clearMovementIntent(EntityId id);
    /** Starts a new physical-input frame: clears ephemeral movement intents
     *  (Platformer, Top Down, simple movers). Jump requests are preserved. */
    void clearFrameMovementIntents();
    /** @deprecated Prefer clearFrameMovementIntents(); kept as a Top Down alias. */
    void clearTopDownMovementContributions();
    /** Adds a bounded Top Down direction contribution for the current input frame. */
    void addTopDownMovementContribution(EntityId id, Vec2 direction);
    void requestJump(EntityId id);
    /** Apply movement intent on entities without Platformer/TopDown (Logic Board default). */
    void tickSimpleMovementIntents(float dt);

    friend void WorldInternal::stepPlatformerController(
        World&, EntityId, const PlatformerControllerComponent&, float);
    friend void WorldInternal::stepTopDownController(
        World&, EntityId, const TopDownControllerComponent&, float);

private:
    Modules::RuntimeEntityGateway& entityGateway_;
    Modules::Physics&              physics_;
    Modules::VariableManager&      variables_;

    struct PlatformerRt {
        float coyoteTimer     = 0.f;
        float jumpBufferTimer = 0.f;
        Vec2  velocity        = {};
        /** Previous-frame jump intent; used to arm buffer only on rising edge. */
        bool jumpPendingPrev  = false;
        /** True while the body is attached to a ladder (gravity suspended). */
        bool climbing         = false;
        /** ADR-0052: canonical post-step support; not a live collision re-query. */
        bool grounded         = false;
        /** ADR-0052: canonical post-step locomotion state. */
        PlatformerState state = PlatformerState::Stopped;
        /**
         * Last definite airborne Jumping/Falling (ADR-0016). Used at apex when
         * |vy| ≤ ε so Logic never sees a one-frame Stopped mid-jump.
         */
        PlatformerState lastAirState = PlatformerState::Jumping;
    };
    std::unordered_map<EntityId, PlatformerRt> platformerRt_;
    std::unordered_map<EntityId, PlatformerStepContacts> platformerStepContacts_;
    std::unordered_map<EntityId, PlatformerContactProjection> platformerContacts_;

    struct TopDownRt {
        Vec2 velocity;
    };
    std::unordered_map<EntityId, TopDownRt> topDownRt_;

    struct ControlIntent {
        Vec2 movement;
        bool hasMovement   = false;
        bool jumpRequested = false;
        /** Queued by PostSimulation; promoted at controller start. */
        std::optional<WallJumpIntent> nextWallJump;
        std::optional<WallSlideIntent> nextWallSlide;
    };
    std::unordered_map<EntityId, ControlIntent> controlIntents_;

    CollisionWorld::World              collisionWorld_;
    std::vector<CollisionWorld::ContactEvent> collisionEvents_;
    std::vector<PhysicsLayerDef>       physicsLayers_;

    TilemapData  activeTilemap_;
    std::unordered_map<int, TileSurfaceMeta> tileMeta_;

    void applyTilePalette(const std::vector<TilePaletteEntry>& tilePalette);

    void clearGameplayRuntimeState();
    /** Drop per-entity gameplay caches when the gateway destroys entity id. */
    void forgetEntity(EntityId id);

    /**
     * ADR-0022 / ADR-0052: single ground-support query (eligibility + correctionY).
     * When allowFloorSnap is true, also accepts downward gaps up to the scaled
     * maxFloorSnapDistance (post-Y snap only).
     */
    std::optional<GroundSupport> findGroundSupport(
        EntityId id,
        const Transform& current,
        const Transform& previous,
        float requestedVerticalVelocity,
        bool allowFloorSnap = false) const;

    /** ADR-0052 / ADR-0054: Platformer-only axis resolution (does not alter the other axis). */
    PlatformerXMoveResult movePlatformerX(
        EntityId id,
        Transform& transform,
        const Transform& beforeMove,
        float& horizontalVelocity) const;
    PlatformerYMoveResult movePlatformerY(
        EntityId id,
        Transform& transform,
        const Transform& beforeY,
        float& verticalVelocity) const;

    /** ADR-0021: Top Down owns Transform; resolves vs CollisionWorld (push Physics only). */
    void tickTopDownControllers(float dt);
    void tickLinearMovers(float dt);
    void tickMagneticItems(float dt);
    void tickHordeMembers(float dt);

    enum class CameraFollowMode {
        Automatic,
        Explicit,
        Disabled,
    };
    CameraFollowMode cameraFollowMode_ = CameraFollowMode::Automatic;
    EntityId cameraFollowTarget_ = INVALID_ENTITY;
    Vec2 cameraCenter_{};

    Modules::Renderer* renderer_ = nullptr;
    Modules::SpriteAnimator* spriteAnimator_ = nullptr;
    std::unordered_set<EntityId> pendingGameplayDestroyIds_;
    std::unordered_set<EntityId> destroyingEntityIds_;
    std::function<void(EntityId)> entityWillDestroyHandler_;
    std::function<void(EntityId)> entityDestroyedHandler_;
    Modules::SceneLifecycleService* lifecycle_ = nullptr;
};

} // namespace ArtCade
