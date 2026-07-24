#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"   // Vec2
#include <functional>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * CameraManager — 2D camera system.
 *
 * Features:
 *   - position, rotation, zoom (all smoothly tweened via lerp)
 *   - optional follow-target (supply a getter lambda; camera lerps towards it)
 *   - screen-shake: trauma-based, decays over time
 *   - shakeModifiers(): render-only offsets for Presentation (not authoritative position)
 *   - visible bounds from authoritative position + zoom (surface mapping: Presentation Core)
 *   - viewport bounds: the rectangle of the world visible through this camera
 *
 * The Renderer reads position()/rotation()/zoom() and applies them.
 * This module does NOT call Raylib — it just manages camera state.
 */
class CameraManager final : public IModule {
public:
    CameraManager() = default;

    bool init()     override;
    void shutdown() override;

    // ------------------------------------------------------------------ direct control

    void setPosition(Vec2 pos);
    void setRotation(float radians);
    void setZoom(float zoom);

    /** Authoritative simulation position (excludes shake modifiers). */
    Vec2  position() const;
    /** Authoritative simulation rotation (excludes shake modifiers). */
    float rotation() const;
    float zoom()     const;

    // ------------------------------------------------------------------ smooth movement

    // Lerp camera toward a target position over time
    void moveTo(Vec2 target, float duration);

    // Zoom smoothly
    void zoomTo(float target, float duration);

    // Snap immediately (no lerp)
    void snapTo(Vec2 pos);
    void snapZoom(float zoom);

    // ------------------------------------------------------------------ follow

    using TargetGetter = std::function<Vec2()>;

    // Supply a lambda that returns the target world position each frame
    void setFollowTarget(TargetGetter getter, float lerpSpeed = 5.f);
    void clearFollowTarget();

    // ------------------------------------------------------------------ shake

    // Add trauma (0–1 range; stacks additively, clamped to 1).
    // durationSeconds > 0 sets linear decay so trauma≈1 reaches ~0 in that time.
    void addTrauma(float amount, float durationSeconds = 0.f);
    float trauma() const { return trauma_; }

    // Max shake displacement in world units and max rotation in radians
    void setShakeParams(float maxDisplace, float maxRotation);

    // Current shake displacement (computed in update()). Render code can add
    // this to the Renderer's base camera position for the frame and subtract
    // it afterwards, so screen shake stays a pure visual offset and never
    // bleeds into authoritative game-state camera coordinates.
    Vec2  shakeOffset()       const { return shakeOffset_; }
    float shakeRotationOffset() const { return shakeRotOffset_; }

    // ------------------------------------------------------------------ update

    void update(float dt);
    /** Follow / lerp / zoom only (safe before gameplay). */
    void updateMotion(float dt);
    /** Recompute shake offset from current trauma (no decay). */
    void refreshShakeOffset(float dt);
    /** Decay trauma after simulation for this frame. */
    void decayTrauma(float dt);
    /** refreshShakeOffset + decayTrauma (legacy single-step helper). */
    void updateShake(float dt);

    // ------------------------------------------------------------------ viewport

    /** Screen size must be provided so the camera can compute visible bounds */
    void setScreenSize(float w, float h);

    /** Returns the world-space AABB [topLeft, bottomRight] currently visible */
    void visibleBounds(Vec2& outTopLeft, Vec2& outBottomRight) const;

private:
    Vec2  pos_      = { 0.f, 0.f };
    float rotation_ = 0.f;
    float zoom_     = 1.f;

    // Position lerp
    Vec2  movTarget_   = { 0.f, 0.f };
    float movDuration_ = 0.f;
    float movElapsed_  = 0.f;
    Vec2  movStart_    = { 0.f, 0.f };

    // Zoom lerp
    float zoomTarget_   = 1.f;
    float zoomDuration_ = 0.f;
    float zoomElapsed_  = 0.f;
    float zoomStart_    = 1.f;

    // Follow
    TargetGetter followGetter_;
    float        followSpeed_ = 5.f;

    // Shake
    float trauma_           = 0.f;
    float traumaDecayRate_  = 1.5f;
    float shakeDisplace_    = 32.f;
    float shakeRotation_  = 0.05f;
    Vec2  shakeOffset_    = { 0.f, 0.f };
    float shakeRotOffset_ = 0.f;
    float shakeTime_      = 0.f;  // used as noise seed

    // Screen
    float screenW_ = 1280.f;
    float screenH_ = 720.f;

    float lerpf(float a, float b, float t) const;
    Vec2  lerpv(Vec2 a, Vec2 b, float t)   const;
    float pseudoNoise(float x) const;
};

} // namespace ArtCade::Modules
