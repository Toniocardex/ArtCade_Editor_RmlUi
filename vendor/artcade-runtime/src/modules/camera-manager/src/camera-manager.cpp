#include "../include/camera-manager.h"
#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

bool CameraManager::init() {
    pos_      = { 0.f, 0.f };
    rotation_ = 0.f;
    zoom_     = 1.f;
    movDuration_ = zoomDuration_ = 0.f;
    followGetter_ = {};
    trauma_          = 0.f;
    traumaDecayRate_ = 1.5f;
    shakeOffset_     = { 0.f, 0.f };
    shakeRotOffset_ = 0.f;
    shakeTime_    = 0.f;
    return true;
}

void CameraManager::shutdown() {
    followGetter_ = {};
}

// ------------------------------------------------------------------ direct control

void CameraManager::setPosition(Vec2 pos) { pos_ = pos; }
void CameraManager::setRotation(float r)  { rotation_ = r; }
void CameraManager::setZoom(float z)      { zoom_ = z; }

Vec2  CameraManager::position() const { return pos_; }
float CameraManager::rotation() const { return rotation_; }
float CameraManager::zoom()     const { return zoom_; }

// ------------------------------------------------------------------ smooth movement

void CameraManager::moveTo(Vec2 target, float duration) {
    if (duration <= 0.f) { pos_ = target; movDuration_ = 0.f; return; }
    movStart_    = pos_;
    movTarget_   = target;
    movDuration_ = duration;
    movElapsed_  = 0.f;
}

void CameraManager::zoomTo(float target, float duration) {
    if (duration <= 0.f) { zoom_ = target; zoomDuration_ = 0.f; return; }
    zoomStart_    = zoom_;
    zoomTarget_   = target;
    zoomDuration_ = duration;
    zoomElapsed_  = 0.f;
}

void CameraManager::snapTo(Vec2 pos)   { pos_ = pos;  movDuration_ = 0.f; }
void CameraManager::snapZoom(float z)  { zoom_ = z;   zoomDuration_ = 0.f; }

// ------------------------------------------------------------------ follow

void CameraManager::setFollowTarget(TargetGetter getter, float speed) {
    followGetter_ = std::move(getter);
    followSpeed_  = speed;
}

void CameraManager::clearFollowTarget() {
    followGetter_ = {};
}

// ------------------------------------------------------------------ shake

void CameraManager::addTrauma(float amount, float durationSeconds) {
    if (amount <= 0.f) return;
    trauma_ = std::min(trauma_ + amount, 1.f);
    if (durationSeconds > 0.f)
        traumaDecayRate_ = 1.f / durationSeconds;
}

void CameraManager::setShakeParams(float maxDisplace, float maxRotation) {
    shakeDisplace_  = maxDisplace;
    shakeRotation_  = maxRotation;
}

// ------------------------------------------------------------------ update

float CameraManager::lerpf(float a, float b, float t) const {
    return a + (b - a) * t;
}

Vec2 CameraManager::lerpv(Vec2 a, Vec2 b, float t) const {
    return { lerpf(a.x, b.x, t), lerpf(a.y, b.y, t) };
}

// Simple pseudo-noise from a float seed (no stdlib random needed)
float CameraManager::pseudoNoise(float x) const {
    float s = std::sin(x * 127.1f + 311.7f) * 43758.5453f;
    s -= std::floor(s);
    return s * 2.f - 1.f;   // range [-1, 1]
}

void CameraManager::updateMotion(float dt) {
    // Follow target (overrides lerp tween if both active)
    if (followGetter_) {
        Vec2 target = followGetter_();
        float t = std::min(followSpeed_ * dt, 1.f);
        pos_ = lerpv(pos_, target, t);
        movDuration_ = 0.f;
    }

    // Position lerp tween
    if (movDuration_ > 0.f) {
        movElapsed_ += dt;
        float t = std::min(movElapsed_ / movDuration_, 1.f);
        pos_ = lerpv(movStart_, movTarget_, t);
        if (t >= 1.f) movDuration_ = 0.f;
    }

    // Zoom lerp tween
    if (zoomDuration_ > 0.f) {
        zoomElapsed_ += dt;
        float t = std::min(zoomElapsed_ / zoomDuration_, 1.f);
        zoom_ = lerpf(zoomStart_, zoomTarget_, t);
        if (t >= 1.f) zoomDuration_ = 0.f;
    }
}

void CameraManager::refreshShakeOffset(float dt) {
    if (trauma_ > 0.f) {
        // Linear in trauma so Logic Board intensity 0–1 maps predictably to amplitude.
        const float shake = trauma_;
        shakeTime_ += dt;
        shakeOffset_.x = shakeDisplace_ * shake * pseudoNoise(shakeTime_ * 17.f);
        shakeOffset_.y = shakeDisplace_ * shake * pseudoNoise(shakeTime_ * 13.f + 5.f);
        shakeRotOffset_ = shakeRotation_ * shake * pseudoNoise(shakeTime_ * 23.f + 3.f);
    } else {
        shakeOffset_    = { 0.f, 0.f };
        shakeRotOffset_ = 0.f;
    }
}

void CameraManager::decayTrauma(float dt) {
    if (trauma_ > 0.f)
        trauma_ = std::max(0.f, trauma_ - dt * traumaDecayRate_);
}

void CameraManager::updateShake(float dt) {
    refreshShakeOffset(dt);
    decayTrauma(dt);
}

void CameraManager::update(float dt) {
    updateMotion(dt);
    updateShake(dt);
}

void CameraManager::setScreenSize(float w, float h) {
    screenW_ = w;
    screenH_ = h;
}

void CameraManager::visibleBounds(Vec2& tl, Vec2& br) const {
    float hw = (screenW_ * 0.5f) / zoom_;
    float hh = (screenH_ * 0.5f) / zoom_;
    Vec2 cam = position();
    tl = { cam.x - hw, cam.y - hh };
    br = { cam.x + hw, cam.y + hh };
}

} // namespace ArtCade::Modules
