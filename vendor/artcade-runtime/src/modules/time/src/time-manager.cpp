#include "../include/time-manager.h"
#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

// ------------------------------------------------------------------ init / shutdown

bool TimeManager::init() {
    initDefaultLayers();
    return true;
}

void TimeManager::shutdown() {
    timers_.clear();
    pauseStack_.clear();
    layers_.clear();
    realElapsed_ = gameElapsed_ = realDelta_ = 0.f;
}

void TimeManager::initDefaultLayers() {
    layers_["gameplay"]  = { 1.f, 1.f, 0.f, 0.f, 1.f, true,  0.f };
    layers_["ui"]        = { 1.f, 1.f, 0.f, 0.f, 1.f, false, 0.f };
    layers_["audio"]     = { 1.f, 1.f, 0.f, 0.f, 1.f, false, 0.f };
    layers_["physics"]   = { 1.f, 1.f, 0.f, 0.f, 1.f, false, 0.f };
    layers_["realtime"]  = { 1.f, 1.f, 0.f, 0.f, 1.f, false, 0.f };
}

// ------------------------------------------------------------------ tick

void TimeManager::tick(float realDelta) {
    realDelta_    = realDelta;
    realElapsed_ += realDelta;

    for (auto& [name, layer] : layers_) {
        updateLayer(layer, realDelta);
    }

    // gameElapsed_ is the canonical "game time" returned by Lua's time.now().
    // It MUST track a single layer (gameplay) — previously the loop above
    // accumulated once per layer, so with the 5 default layers the value
    // advanced ~5× faster than real time, breaking every cooldown / timer
    // tied to time.now().
    auto gameplay = layers_.find("gameplay");
    if (gameplay != layers_.end())
        gameElapsed_ = gameplay->second.elapsed;

    updateTimers(realDelta);
}

void TimeManager::updateLayer(TimeLayer& layer, float realDelta) {
    // Smooth scale transition
    if (layer.transitionDur > 0.f) {
        layer.transitionElapsed += realDelta;
        float t = std::min(layer.transitionElapsed / layer.transitionDur, 1.f);
        layer.scale = layer.startScale + (layer.targetScale - layer.startScale) * t;
        if (t >= 1.f) {
            layer.scale       = layer.targetScale;
            layer.transitionDur = 0.f;
        }
    }

    float effective = (isPaused() && layer.affectedByPause) ? 0.f : layer.scale;
    layer.elapsed += effective * realDelta;
}

void TimeManager::updateTimers(float realDelta) {
    for (auto& timer : timers_) {
        if (timer.cancelled) continue;

        auto it = layers_.find(timer.layer);
        float effective = 1.f;
        if (it != layers_.end()) {
            effective = (isPaused() && it->second.affectedByPause)
                        ? 0.f : it->second.scale;
        }

        timer.remaining -= effective * realDelta;
        if (timer.remaining <= 0.f) {
            timer.cb();
            if (timer.repeat)
                timer.remaining += timer.interval;
            else
                timer.cancelled = true;
        }
    }

    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
        [](const Timer& t) { return t.cancelled && !t.repeat; }),
        timers_.end());
}

// ------------------------------------------------------------------ time queries

float TimeManager::now()     const { return gameElapsed_; }
float TimeManager::realNow() const { return realElapsed_; }

float TimeManager::delta(const std::string& layer) const {
    auto it = layers_.find(layer);
    if (it == layers_.end()) return 0.f;
    if (isPaused() && it->second.affectedByPause) return 0.f;
    return it->second.scale * realDelta_;
}

// ------------------------------------------------------------------ scale

void TimeManager::setTimeScale(float scale, const std::string& layer, float duration) {
    auto& l = layers_[layer];
    if (duration <= 0.f) {
        l.scale = l.targetScale = scale;
        l.transitionDur = 0.f;
    } else {
        l.startScale        = l.scale;
        l.targetScale       = scale;
        l.transitionDur     = duration;
        l.transitionElapsed = 0.f;
    }
}

float TimeManager::timeScale(const std::string& layer) const {
    auto it = layers_.find(layer);
    return (it != layers_.end()) ? it->second.scale : 1.f;
}

// ------------------------------------------------------------------ pause

uint32_t TimeManager::pause(const std::string& source, int priority) {
    uint32_t token = nextToken_++;
    pauseStack_.push_back({ token, source, priority });
    std::sort(pauseStack_.begin(), pauseStack_.end(),
        [](const PauseRequest& a, const PauseRequest& b){
            return a.priority > b.priority;
        });
    return token;
}

void TimeManager::resume(uint32_t token) {
    pauseStack_.erase(
        std::remove_if(pauseStack_.begin(), pauseStack_.end(),
            [token](const PauseRequest& r){ return r.token == token; }),
        pauseStack_.end());
}

void TimeManager::resumeSource(const std::string& source) {
    pauseStack_.erase(
        std::remove_if(pauseStack_.begin(), pauseStack_.end(),
            [&source](const PauseRequest& r){ return r.source == source; }),
        pauseStack_.end());
}

bool TimeManager::isPaused() const {
    return !pauseStack_.empty();
}

bool TimeManager::isPauseSourceActive(const std::string& source) const {
    for (const auto& r : pauseStack_)
        if (r.source == source) return true;
    return false;
}

// ------------------------------------------------------------------ timers

uint32_t TimeManager::delay(float seconds, TimerCallback cb, const std::string& layer) {
    uint32_t id = nextTimerId_++;
    timers_.push_back({ id, seconds, seconds, false, std::move(cb), layer, false });
    return id;
}

uint32_t TimeManager::every(float interval, TimerCallback cb, const std::string& layer) {
    uint32_t id = nextTimerId_++;
    timers_.push_back({ id, interval, interval, true, std::move(cb), layer, false });
    return id;
}

void TimeManager::cancelTimer(uint32_t timerId) {
    for (auto& t : timers_)
        if (t.id == timerId) { t.cancelled = true; break; }
}

} // namespace ArtCade::Modules
