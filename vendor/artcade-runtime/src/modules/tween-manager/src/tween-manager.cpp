#include "../include/tween-manager.h"
#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

static constexpr float kPi = 3.14159265358979323846f;

bool TweenManager::init() {
    tweens_.clear();
    nextId_ = 1;
    return true;
}

void TweenManager::shutdown() {
    tweens_.clear();
}

// ------------------------------------------------------------------ easing

float TweenManager::applyEase(Ease ease, float t) {
    t = std::max(0.f, std::min(1.f, t));
    switch (ease) {
    case Ease::Linear:     return t;
    case Ease::QuadIn:     return t * t;
    case Ease::QuadOut:    return t * (2.f - t);
    case Ease::QuadInOut:  return (t < 0.5f) ? 2.f*t*t : -1.f + (4.f - 2.f*t)*t;
    case Ease::CubicIn:    return t * t * t;
    case Ease::CubicOut: { float u = 1.f - t; return 1.f - u*u*u; }
    case Ease::CubicInOut:
        return (t < 0.5f) ? 4.f*t*t*t : (t-1.f)*(2.f*t-2.f)*(2.f*t-2.f)+1.f;
    case Ease::SineIn:     return 1.f - std::cos(t * kPi * 0.5f);
    case Ease::SineOut:    return std::sin(t * kPi * 0.5f);
    case Ease::SineInOut:  return -(std::cos(kPi * t) - 1.f) * 0.5f;
    case Ease::ElasticOut: {
        if (t == 0.f || t == 1.f) return t;
        float p = 0.3f;
        return std::pow(2.f, -10.f*t) * std::sin((t - p/4.f) * 2.f*kPi / p) + 1.f;
    }
    case Ease::BounceOut: {
        if (t < 1.f/2.75f) return 7.5625f * t * t;
        if (t < 2.f/2.75f) { t -= 1.5f/2.75f;   return 7.5625f*t*t + 0.75f; }
        if (t < 2.5f/2.75f){ t -= 2.25f/2.75f;  return 7.5625f*t*t + 0.9375f; }
        t -= 2.625f/2.75f; return 7.5625f*t*t + 0.984375f;
    }
    case Ease::BackOut: {
        float c1 = 1.70158f;
        float u  = t - 1.f;
        return 1.f + (c1 + 1.f) * u*u*u + c1 * u*u;
    }
    }
    return t;
}

// ------------------------------------------------------------------ create

TweenManager::TweenId TweenManager::tween(Params p) {
    TweenEntry e;
    e.id              = nextId_++;
    e.from            = p.from;
    e.to              = p.to;
    e.duration        = p.duration > 0.f ? p.duration : 0.0001f;
    e.ease            = p.ease;
    e.delay           = p.delay;
    e.delayRemaining  = p.delay;
    e.loop            = p.loop;
    e.pingPong        = p.pingPong;
    e.elapsed         = 0.f;
    e.state           = State::Active;
    e.onUpdate        = std::move(p.onUpdate);
    e.onComplete      = std::move(p.onComplete);
    tweens_.push_back(std::move(e));
    return tweens_.back().id;
}

TweenManager::TweenId TweenManager::tweenTo(float from, float to, float duration,
                                             Ease ease, UpdateCb onUpdate,
                                             CompleteCb onComplete) {
    return tween({ from, to, duration, ease, 0.f, false, false,
                   std::move(onUpdate), std::move(onComplete) });
}

// ------------------------------------------------------------------ control

void TweenManager::pause(TweenId id) {
    for (auto& e : tweens_)
        if (e.id == id && e.state == State::Active) { e.state = State::Paused; break; }
}

void TweenManager::resume(TweenId id) {
    for (auto& e : tweens_)
        if (e.id == id && e.state == State::Paused) { e.state = State::Active; break; }
}

void TweenManager::cancel(TweenId id) {
    for (auto& e : tweens_)
        if (e.id == id) { e.state = State::Done; break; }
}

void TweenManager::cancelAll() {
    for (auto& e : tweens_) e.state = State::Done;
}

bool TweenManager::isActive(TweenId id) const {
    for (const auto& e : tweens_)
        if (e.id == id) return e.state == State::Active;
    return false;
}

// ------------------------------------------------------------------ update

void TweenManager::update(float dt) {
    for (auto& e : tweens_) {
        if (e.state != State::Active) continue;

        // Delay phase — quando il delay scade a metà frame, il tempo
        // rimanente viene applicato al tween nello stesso frame.
        float effectiveDt = dt;
        if (e.delayRemaining > 0.f) {
            e.delayRemaining -= dt;
            if (e.delayRemaining > 0.f) continue;
            effectiveDt = -e.delayRemaining;   // overshoot oltre il delay
            e.delayRemaining = 0.f;
        }

        e.elapsed += effectiveDt;
        float t = std::min(e.elapsed / e.duration, 1.f);
        float easedT = applyEase(e.ease, e.reverse ? 1.f - t : t);
        float value  = e.from + (e.to - e.from) * easedT;

        if (e.onUpdate) e.onUpdate(value);

        if (e.elapsed >= e.duration) {
            if (e.pingPong) {
                e.reverse  = !e.reverse;
                e.elapsed  = 0.f;
                if (!e.loop && e.reverse == false) {
                    // Completed a full ping-pong cycle
                    e.state = State::Done;
                    if (e.onComplete) e.onComplete();
                }
            } else if (e.loop) {
                e.elapsed = 0.f;
            } else {
                e.state = State::Done;
                if (e.onComplete) e.onComplete();
            }
        }
    }

    // Prune completed tweens
    tweens_.erase(
        std::remove_if(tweens_.begin(), tweens_.end(),
            [](const TweenEntry& e){ return e.state == State::Done; }),
        tweens_.end());
}

} // namespace ArtCade::Modules
