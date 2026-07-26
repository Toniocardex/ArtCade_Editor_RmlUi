#pragma once

#include "types.h"

#include <optional>
#include <string>

namespace ArtCade {

// Single gameplay mutation/query boundary shared by generated Logic programs
// and manual Script programs. Implementations belong to the materialized
// runtime (Editor Play or standalone game), never to either language runtime.
class IGameplayRuntimeHost {
public:
    virtual ~IGameplayRuntimeHost() = default;
    virtual bool setVisible(EntityId owner, bool value) = 0;
    /** Runtime visibility query for Self (Logic Is Visible event). */
    virtual bool isVisible(EntityId owner) = 0;
    /**
     * Horizontal mirror for Self's drawable sprite.
     * Convention: art faces Right when flipX is false; Left when true.
     */
    virtual bool setSpriteFlipX(EntityId owner, bool flipX) = 0;
    virtual bool setPosition(EntityId owner, Vec2 value) = 0;
    /** World position of @p owner; nullopt when inactive. */
    virtual std::optional<Vec2> getPosition(EntityId owner) const = 0;
    /** Active scene world size in world units; nullopt when unavailable. */
    virtual std::optional<Vec2> getSceneWorldSize() const = 0;
    virtual bool translate(EntityId owner, Vec2 delta) = 0;
    /** Absolute rotation in radians (Logic compiler converts authored degrees). */
    virtual bool setRotation(EntityId owner, float radians) = 0;
    /** Relative rotation delta in radians. */
    virtual bool rotateBy(EntityId owner, float deltaRadians) = 0;
    /** Absolute scale; both axes must be finite and > 0 (no negative flip). */
    virtual bool setScale(EntityId owner, Vec2 scale) = 0;
    virtual bool isGrounded(EntityId owner) = 0;
    /**
     * True while Self is airborne and moving downward (+Y down).
     * Not the same as !isGrounded() — rising after a jump is false.
     */
    virtual bool isFalling(EntityId owner) = 0;
    /**
     * Mutually exclusive platformer locomotion (ADR-0016).
     * Prefer over composing isGrounded/isFalling/isPlatformerMoving.
     */
    virtual PlatformerState platformerState(EntityId owner) = 0;
    /**
     * True while platformerState is Moving (thin wrapper for Script parity).
     */
    virtual bool isPlatformerMoving(EntityId owner) = 0;
    virtual bool requestPlatformerMove(EntityId owner, float axis) = 0;
    /** Adds this input frame's Top Down movement direction for Self. */
    virtual bool requestTopDownMove(EntityId owner, Vec2 direction) = 0;
    virtual bool requestPlatformerJump(EntityId owner) = 0;
    virtual bool isObjectType(EntityId entity, const ObjectTypeId& objectTypeId) = 0;
    virtual bool requestDestroy(EntityId owner) = 0;
    virtual bool playAnimationClip(EntityId owner, const AssetId& animationAssetId,
                                   const std::string& clipId) = 0;
    virtual bool stopAnimation(EntityId owner) = 0;
    virtual bool setAnimationPlaybackSpeed(EntityId owner, float speed) = 0;
    virtual bool playSound(EntityId owner, const AssetId& audioAssetId, float volume) = 0;
    /**
     * Sets a catalog Number global. Unknown key or wrong type → false.
     * Does not create variables.
     */
    virtual bool setStateNumber(const GameVariableId& id, double value) = 0;
    /**
     * Adds delta to a catalog Number global. Unknown key or wrong type → false.
     * Does not create variables.
     */
    virtual bool addStateNumber(const GameVariableId& id, double delta) = 0;
    /**
     * Toggles a catalog Boolean global. Unknown key or wrong type → false.
     */
    virtual bool toggleStateBoolean(const GameVariableId& id) = 0;
    /**
     * Reads a catalog Number global. Unknown key or wrong type → nullopt.
     * Never invents a default zero for Compare Variable.
     */
    virtual std::optional<double> getStateNumber(const GameVariableId& id) const = 0;
    /**
     * Reads a Number local variable on @p owner. Unknown key or wrong type →
     * nullopt. Default: unavailable (hosts that materialize locals override).
     */
    virtual std::optional<double> getLocalNumber(EntityId owner,
                                                 const GameVariableId& id) const {
        (void)owner;
        (void)id;
        return std::nullopt;
    }
    virtual bool setVelocity(EntityId owner, Vec2 velocity) = 0;
    virtual bool isKeyDown(LogicKey key) = 0;
    /**
     * Spawns an Object Type instance at world position. Returns INVALID_ENTITY on failure.
     * Implementations should install Logic/Script scopes for the new entity when applicable.
     */
    virtual EntityId spawnObjectType(EntityId owner, const ObjectTypeId& objectTypeId,
                                     float x, float y) = 0;
    /**
     * Requests a restart of the active scene (authored layout restored, On Start
     * re-dispatched). Deferred: implementations must apply it at a safe point
     * after the current event dispatch, never synchronously mid-callback.
     * Multiple scene requests in the same frame: the last one wins.
     */
    virtual bool requestSceneRestart() = 0;
    /**
     * Requests switching Play to @p sceneId. Same deferred, last-wins contract
     * as requestSceneRestart(). Empty ids are rejected with false.
     */
    virtual bool requestSceneGoTo(const SceneId& sceneId) = 0;
};

} // namespace ArtCade
